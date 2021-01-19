/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women�s Hospital through NIH grant R01MH112748.

==============================================================================*/

// VTK includes
#include <vtkActor2D.h>
#include <vtkArcSource.h>
#include <vtkAppendPolyData.h>
#include <vtkCellLocator.h>
#include <vtkClipPolyData.h>
#include <vtkContourTriangulator.h>
#include <vtkCompositeDataGeometryFilter.h>
#include <vtkDiscretizableColorTransferFunction.h>
#include <vtkDoubleArray.h>
#include <vtkFeatureEdges.h>
#include <vtkGlyph2D.h>
#include <vtkLine.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkObjectFactory.h>
#include <vtkParametricEllipsoid.h>
#include <vtkParametricFunctionSource.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPlane.h>
#include <vtkPlaneCutter.h>
#include <vtkPlaneSource.h>
#include <vtkPoints.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkSampleImplicitFunctionFilter.h>
#include <vtkSlicerROIRepresentation2D.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>

// MRML includes
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsROINode.h"
#include "vtkMRMLProceduralColorNode.h"

#include "vtkMRMLMarkupsPlaneNode.h"

#include "vtkMarkupsGlyphSource2D.h"

#include <vtkCutter.h>

vtkStandardNewMacro(vtkSlicerROIRepresentation2D);

#include <vtkCubeSource.h>

//----------------------------------------------------------------------
vtkSlicerROIRepresentation2D::vtkSlicerROIRepresentation2D()
{
  this->ROISource= nullptr;

  this->ROIToWorldTransform = vtkSmartPointer<vtkTransform>::New();
  this->ROITransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROITransformFilter->SetTransform(this->ROIToWorldTransform);

  this->ROIOutlineCutter = vtkSmartPointer<vtkCutter>::New();
  this->ROIOutlineCutter->SetInputConnection(this->ROITransformFilter->GetOutputPort());
  this->ROIOutlineCutter->SetCutFunction(this->SlicePlane);

  this->ROIOutlineWorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROIOutlineWorldToSliceTransformFilter->SetInputConnection(this->ROIOutlineCutter->GetOutputPort());
  this->ROIOutlineWorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);

  this->OutlineFilter = vtkSmartPointer<vtkOutlineFilter>::New();

  this->ROIWorldToSliceTransformFilter2A = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROIWorldToSliceTransformFilter2A->SetInputConnection(OutlineFilter->GetOutputPort());
  this->ROIWorldToSliceTransformFilter2A->SetTransform(this->ROIToWorldTransform);

  PlaneClipperSlicePlane->SetInputConnection(this->ROIWorldToSliceTransformFilter2A->GetOutputPort());
  PlaneClipperSlicePlane->SetClipFunction(this->SlicePlane);
  PlaneClipperSlicePlane->GenerateClippedOutputOn();

  PlaneClipperStartFadeNear->SetInputConnection(PlaneClipperSlicePlane->GetOutputPort(0));
  PlaneClipperStartFadeNear->SetClipFunction(this->SlicePlane);
  PlaneClipperStartFadeNear->GenerateClippedOutputOn();

  PlaneClipperEndFadeNear->SetInputConnection(PlaneClipperStartFadeNear->GetOutputPort(0));
  PlaneClipperEndFadeNear->SetClipFunction(this->SlicePlane);
  PlaneClipperEndFadeNear->GenerateClippedOutputOn();

  PlaneClipperStartFadeFar->SetInputConnection(PlaneClipperSlicePlane->GetOutputPort(1));
  PlaneClipperStartFadeFar->SetClipFunction(this->SlicePlane);
  PlaneClipperStartFadeFar->GenerateClippedOutputOn();

  PlaneClipperEndFadeFar->SetInputConnection(PlaneClipperStartFadeFar->GetOutputPort(1));
  PlaneClipperEndFadeFar->SetClipFunction(this->SlicePlane);
  PlaneClipperEndFadeFar->GenerateClippedOutputOn();

  vtkNew<vtkAppendPolyData> PlaneAppend;
  PlaneAppend->AddInputConnection(PlaneClipperStartFadeNear->GetOutputPort(1));
  PlaneAppend->AddInputConnection(PlaneClipperEndFadeNear->GetOutputPort(0));
  PlaneAppend->AddInputConnection(PlaneClipperEndFadeNear->GetOutputPort(1));
  PlaneAppend->AddInputConnection(PlaneClipperStartFadeFar->GetOutputPort(0));
  PlaneAppend->AddInputConnection(PlaneClipperEndFadeFar->GetOutputPort(0));
  PlaneAppend->AddInputConnection(PlaneClipperEndFadeFar->GetOutputPort(1));
  PlaneAppend->AddInputConnection(this->ROIOutlineCutter->GetOutputPort());

  this->ROIWorldToSliceTransformFilter2A = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROIWorldToSliceTransformFilter2A->SetInputConnection(PlaneAppend->GetOutputPort());
  this->ROIWorldToSliceTransformFilter2A->SetTransform(this->ROIToWorldTransform);

  vtkNew<vtkAppendPolyData> append;
  append->AddInputConnection(PlaneAppend->GetOutputPort());
  append->AddInputConnection(this->ROIOutlineCutter->GetOutputPort());

  vtkNew<vtkSampleImplicitFunctionFilter> distance2;
  distance2->SetImplicitFunction(this->SlicePlane);
  distance2->SetInputConnection(append->GetOutputPort());

  this->ROIWorldToSliceTransformFilter2B = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROIWorldToSliceTransformFilter2B->SetInputConnection(distance2->GetOutputPort());
  this->ROIWorldToSliceTransformFilter2B->SetTransform(this->WorldToSliceTransform);

  this->OutlineColorMap = vtkSmartPointer<vtkDiscretizableColorTransferFunction>::New();

  this->ROIOutlineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->ROIOutlineMapper->SetInputConnection(this->ROIWorldToSliceTransformFilter2B->GetOutputPort());
  this->ROIOutlineMapper->SetLookupTable(this->OutlineColorMap);
  this->ROIOutlineProperty = vtkSmartPointer<vtkProperty2D>::New();
  this->ROIOutlineProperty->DeepCopy(this->GetControlPointsPipeline(Unselected)->Property);
  this->ROIOutlineActor = vtkSmartPointer<vtkActor2D>::New();
  this->ROIOutlineActor->SetMapper(this->ROIOutlineMapper);
  this->ROIOutlineActor->SetProperty(this->ROIOutlineProperty);

  vtkNew<vtkContourTriangulator> triangulator;
  triangulator->SetInputConnection(this->ROIOutlineWorldToSliceTransformFilter->GetOutputPort());

  this->ROIMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->ROIMapper->SetInputConnection(triangulator->GetOutputPort());
  this->ROIProperty = vtkSmartPointer<vtkProperty2D>::New();
  this->ROIProperty->DeepCopy(this->GetControlPointsPipeline(Unselected)->Property);
  this->ROIActor = vtkSmartPointer<vtkActor2D>::New();
  this->ROIActor->SetMapper(this->ROIMapper);
  this->ROIActor->SetProperty(this->ROIProperty);

}

//----------------------------------------------------------------------
vtkSlicerROIRepresentation2D::~vtkSlicerROIRepresentation2D() = default;

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData /*=nullptr*/)
{
  Superclass::UpdateFromMRML(caller, event, callData);

  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(this->MarkupsNode);
  if (!roiNode)
    {
    return;
    }

  switch (roiNode->GetROIType())
    {
    case vtkMRMLMarkupsROINode::BOX:
    case vtkMRMLMarkupsROINode::BOUNDING_BOX:
      this->UpdateBoxFromMRML(roiNode);
      break;
    case vtkMRMLMarkupsROINode::SPHERE:
      this->UpdateEllipsoidFromMRML(roiNode);
      break;
    default:
      this->ROIActor->SetVisibility(false);
      return;
    }

  this->ROIToWorldTransform->SetMatrix(roiNode->GetROIToWorldMatrix());

  this->ROIWorldToSliceTransformFilter2B->Update();
  if (this->ROIWorldToSliceTransformFilter2B->GetOutput())
    {
    double sliceNormal_XY[4] = { 0.0, 0.0, 1.0, 0.0 };
    double sliceNormal_World[4] = { 0, 0, 1, 0 };
    vtkMatrix4x4* xyToRAS = this->GetSliceNode()->GetXYToRAS();
    xyToRAS->MultiplyPoint(sliceNormal_XY, sliceNormal_World);
    double sliceThicknessMm = vtkMath::Norm(sliceNormal_World);
    double* scalarRange = ROIWorldToSliceTransformFilter2B->GetOutput()->GetScalarRange();
    // If the closest point on the plane is further than a half-slice thickness, then hide the plane
    if (scalarRange[0] > 0.5 * sliceThicknessMm || scalarRange[1] < -0.5 * sliceThicknessMm)
      {
      this->VisibilityOff();
      return;
      }
    }
  this->VisibilityOn();

  int controlPointType = Selected;

  double opacity = this->MarkupsDisplayNode->GetOpacity();

  double fillOpacity = this->MarkupsDisplayNode->GetFillVisibility()
    ? opacity * this->MarkupsDisplayNode->GetFillOpacity() : 0.0;
  this->ROIProperty->DeepCopy(this->GetControlPointsPipeline(controlPointType)->Property);
  this->ROIProperty->SetOpacity(fillOpacity);

  double outlineOpacity = this->MarkupsDisplayNode->GetOutlineVisibility()
    ? opacity * this->MarkupsDisplayNode->GetOutlineOpacity() : 0.0;
  this->ROIOutlineProperty->DeepCopy(this->GetControlPointsPipeline(controlPointType)->Property);
  this->ROIOutlineProperty->SetOpacity(outlineOpacity);

  if (this->MarkupsDisplayNode->GetLineColorNode() && this->MarkupsDisplayNode->GetLineColorNode()->GetColorTransferFunction())
    {
    // Update the line color mapping from the colorNode stored in the markups display node
    this->ROIOutlineMapper->SetLookupTable(this->MarkupsDisplayNode->GetLineColorNode()->GetColorTransferFunction());
    }
  else
    {
    // if there is no line color node, build the color mapping from few varibales
    // (color, opacity, distance fading, saturation and hue offset) stored in the display node
    this->UpdateDistanceColorMap(this->OutlineColorMap, this->ROIOutlineActor->GetProperty()->GetColor());
    this->ROIOutlineMapper->SetLookupTable(this->OutlineColorMap);
    }

  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();

  vtkNew<vtkPlane> planeStartFadeNear;
  planeStartFadeNear->SetOrigin(this->SlicePlane->GetOrigin());
  planeStartFadeNear->SetNormal(this->SlicePlane->GetNormal());
  planeStartFadeNear->Push(displayNode->GetLineColorFadingStart());
  this->PlaneClipperStartFadeNear->SetClipFunction(planeStartFadeNear);

  vtkNew<vtkPlane> planeEndFadeNear;
  planeEndFadeNear->SetOrigin(this->SlicePlane->GetOrigin());
  planeEndFadeNear->SetNormal(this->SlicePlane->GetNormal());
  planeEndFadeNear->Push(displayNode->GetLineColorFadingEnd());
  this->PlaneClipperEndFadeNear->SetClipFunction(planeEndFadeNear);

  vtkNew<vtkPlane> planeStartFadeFar;
  planeStartFadeFar->SetOrigin(this->SlicePlane->GetOrigin());
  planeStartFadeFar->SetNormal(this->SlicePlane->GetNormal());
  planeStartFadeFar->Push(-1.0 * displayNode->GetLineColorFadingStart());
  this->PlaneClipperStartFadeFar->SetClipFunction(planeStartFadeFar);

  vtkNew<vtkPlane> planeEndFadeFar;
  planeEndFadeFar->SetOrigin(this->SlicePlane->GetOrigin());
  planeEndFadeFar->SetNormal(this->SlicePlane->GetNormal());
  planeEndFadeFar->Push(-1.0 * displayNode->GetLineColorFadingEnd());
  this->PlaneClipperEndFadeFar->SetClipFunction(planeEndFadeFar);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::SetROISource(vtkPolyDataAlgorithm* roiSource)
{
  this->ROISource = roiSource;
  if (this->ROISource)
    {
    this->ROITransformFilter->SetInputConnection(roiSource->GetOutputPort());
    this->OutlineFilter->SetInputConnection(roiSource->GetOutputPort());
    }
  else
    {
    this->ROITransformFilter->RemoveAllInputConnections(0);
    }
}


//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateBoxFromMRML(vtkMRMLMarkupsROINode* roiNode)
{
  if (!roiNode)
    {
    return;
    }

  vtkSmartPointer<vtkCubeSource> cubeSource = vtkCubeSource::SafeDownCast(this->ROISource);
  if (!cubeSource)
    {
    cubeSource = vtkSmartPointer<vtkCubeSource>::New();
    this->SetROISource(cubeSource);
    }

  double sideLengths[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetSideLengths(sideLengths);
  cubeSource->SetXLength(sideLengths[0]);
  cubeSource->SetYLength(sideLengths[1]);
  cubeSource->SetZLength(sideLengths[2]);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateEllipsoidFromMRML(vtkMRMLMarkupsROINode* roiNode)
{
  if (!roiNode)
    {
    return;
    }

  vtkSmartPointer<vtkParametricFunctionSource> parametricSource = vtkParametricFunctionSource::SafeDownCast(this->ROISource);
  if (!parametricSource)
    {
    parametricSource = vtkSmartPointer<vtkParametricFunctionSource>::New();
    this->SetROISource(parametricSource);
    }

  vtkSmartPointer<vtkParametricEllipsoid> ellipseFunction = vtkParametricEllipsoid::SafeDownCast(parametricSource->GetParametricFunction());
  if (!ellipseFunction)
    {
    ellipseFunction = vtkSmartPointer<vtkParametricEllipsoid>::New();
    parametricSource->SetParametricFunction(ellipseFunction);
    }

  double sideLengths[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetSideLengths(sideLengths);
  ellipseFunction->SetXRadius(sideLengths[0] * 0.5);
  ellipseFunction->SetYRadius(sideLengths[1] * 0.5);
  ellipseFunction->SetZRadius(sideLengths[2] * 0.5);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::GetActors(vtkPropCollection *pc)
{
  this->ROIActor->GetActors(pc);
  this->ROIOutlineActor->GetActors(pc);
  this->Superclass::GetActors(pc);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  this->ROIActor->ReleaseGraphicsResources(win);
  this->ROIOutlineActor->ReleaseGraphicsResources(win);
  this->Superclass::ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderOverlay(vtkViewport *viewport)
{
  int count = Superclass::RenderOverlay(viewport);
  if (this->ROIActor->GetVisibility())
    {
    count +=  this->ROIActor->RenderOverlay(viewport);
    }
  if (this->ROIOutlineActor->GetVisibility())
    {
    count +=  this->ROIOutlineActor->RenderOverlay(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderOpaqueGeometry(viewport);
  if (this->ROIActor->GetVisibility())
    {
    count += this->ROIActor->RenderOpaqueGeometry(viewport);
    }
  if (this->ROIOutlineActor->GetVisibility())
    {
    count += this->ROIOutlineActor->RenderOpaqueGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation2D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderTranslucentPolygonalGeometry(viewport);
  if (this->ROIActor->GetVisibility())
    {
    count += this->ROIActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->ROIOutlineActor->GetVisibility())
    {
    count += this->ROIOutlineActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkSlicerROIRepresentation2D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->ROIActor->GetVisibility() && this->ROIActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->ROIOutlineActor->GetVisibility() && this->ROIOutlineActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//----------------------------------------------------------------------
double* vtkSlicerROIRepresentation2D::GetBounds()
{
  return nullptr;
}

//-----------------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::PrintSelf(ostream& os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::SetupInteractionPipeline()
{
  this->InteractionPipeline = new MarkupsInteractionPipelineROI2D(this);
  this->InteractionPipeline->InitializePipeline();
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::UpdateInteractionPipeline()
{
  Superclass::UpdateInteractionPipeline();
  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode());
  if (!roiNode || !this->MarkupsDisplayNode)
    {
    this->InteractionPipeline->Actor->SetVisibility(false);
    return;
    }

  this->InteractionPipeline->Actor->SetVisibility(this->MarkupsDisplayNode->GetVisibility()
    && this->MarkupsDisplayNode->GetVisibility3D()
    && this->MarkupsDisplayNode->GetHandlesInteractive());

  vtkNew<vtkTransform> handleToWorldTransform;
  handleToWorldTransform->SetMatrix(roiNode->GetInteractionHandleToWorldMatrix());
  this->InteractionPipeline->HandleToWorldTransform->DeepCopy(handleToWorldTransform);

  MarkupsInteractionPipelineROI2D* interactionPipeline = static_cast<MarkupsInteractionPipelineROI2D*>(this->InteractionPipeline);
  interactionPipeline->UpdateScaleHandles();
  interactionPipeline->WorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);
}

//-----------------------------------------------------------------------------
vtkSlicerROIRepresentation2D::MarkupsInteractionPipelineROI2D::MarkupsInteractionPipelineROI2D(vtkSlicerMarkupsWidgetRepresentation* representation)
  : MarkupsInteractionPipelineROI(representation)
{
  this->WorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->WorldToSliceTransformFilter->SetTransform(vtkNew<vtkTransform>());
  this->WorldToSliceTransformFilter->SetInputConnection(this->HandleToWorldTransformFilter->GetOutputPort());
  this->Mapper->SetInputConnection(this->WorldToSliceTransformFilter->GetOutputPort());
  this->Mapper->SetTransformCoordinate(nullptr);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::MarkupsInteractionPipelineROI2D::GetViewPlaneNormal(double viewPlaneNormal[3])
{
  if (!viewPlaneNormal)
    {
    return;
    }

  double viewPlaneNormal4[4] = { 0, 0, 1, 0 };
  if (this->Representation)
    {
    vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(this->Representation->GetViewNode());
    if (sliceNode)
      {
      sliceNode->GetSliceToRAS()->MultiplyPoint(viewPlaneNormal4, viewPlaneNormal4);
      }
    }
  viewPlaneNormal[0] = viewPlaneNormal4[0];
  viewPlaneNormal[1] = viewPlaneNormal4[1];
  viewPlaneNormal[2] = viewPlaneNormal4[2];
}
