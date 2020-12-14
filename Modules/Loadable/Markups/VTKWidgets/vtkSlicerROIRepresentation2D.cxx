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
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/

// VTK includes
#include <vtkActor2D.h>
#include <vtkArcSource.h>
#include <vtkAppendPolyData.h>
#include <vtkCellLocator.h>
#include <vtkClipPolyData.h>
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

  this->ROIWorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROIWorldToSliceTransformFilter->SetInputConnection(this->ROITransformFilter->GetOutputPort());
  this->ROIWorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);

  this->ROIMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->ROIMapper->SetInputConnection(this->ROIWorldToSliceTransformFilter->GetOutputPort());
  this->ROIProperty = vtkSmartPointer<vtkProperty2D>::New();
  this->ROIProperty->DeepCopy(this->GetControlPointsPipeline(Unselected)->Property);
  this->ROIActor = vtkSmartPointer<vtkActor2D>::New();
  this->ROIActor->SetMapper(this->ROIMapper);
  this->ROIActor->SetProperty(this->ROIProperty);

  this->ROIOutlineCutter = vtkSmartPointer<vtkCutter>::New();
  this->ROIOutlineCutter->SetInputConnection(this->ROIWorldToSliceTransformFilter->GetOutputPort());
  this->ROIOutlineCutter->SetCutFunction(this->SlicePlane);

  this->ROIOutlineWorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->ROIOutlineWorldToSliceTransformFilter->SetInputConnection(this->ROIOutlineCutter->GetOutputPort());
  this->ROIOutlineWorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);

  this->ROIOutlineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
  this->ROIOutlineMapper->SetInputConnection(this->ROIOutlineWorldToSliceTransformFilter->GetOutputPort());
  this->ROIOutlineProperty = vtkSmartPointer<vtkProperty2D>::New();
  this->ROIOutlineProperty->DeepCopy(this->GetControlPointsPipeline(Unselected)->Property);
  this->ROIOutlineActor = vtkSmartPointer<vtkActor2D>::New();
  this->ROIOutlineActor->SetMapper(this->ROIOutlineMapper);
  this->ROIOutlineActor->SetProperty(this->ROIOutlineProperty);
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
      this->UpdateBoxFromMRML(roiNode);
      break;
    case vtkMRMLMarkupsROINode::SPHERE:
      this->UpdateEllipseFromMRML(roiNode);
      break;
    default:
      this->ROIActor->SetVisibility(false);
      return;
    }

  this->ROIToWorldTransform->SetMatrix(roiNode->GetROIToWorldMatrix());

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
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation2D::SetROISource(vtkPolyDataAlgorithm* roiSource)
{
  this->ROISource = roiSource;
  if (this->ROISource)
    {
    this->ROITransformFilter->SetInputConnection(roiSource->GetOutputPort());
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
void vtkSlicerROIRepresentation2D::UpdateEllipseFromMRML(vtkMRMLMarkupsROINode* roiNode)
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
void vtkSlicerROIRepresentation2D::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if ( !markupsNode || markupsNode->GetLocked() || markupsNode->GetNumberOfControlPoints() < 1
    || !this->GetVisibility() || !interactionEventData )
    {
    return;
    }

  Superclass::CanInteract(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType != vtkMRMLMarkupsDisplayNode::ComponentNone)
    {
    // if mouse is near a control point then select that (ignore the ROI)
    return;
    }

  // TODO: Interact with edge of box?
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
void vtkSlicerROIRepresentation2D::UpdateInteractionPipeline()
{
  Superclass::UpdateInteractionPipeline();
}
