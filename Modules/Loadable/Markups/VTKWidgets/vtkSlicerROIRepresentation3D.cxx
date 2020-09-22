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
#include <vtkAppendPolyData.h>
#include <vtkArcSource.h>
#include <vtkArrayCalculator.h>
#include <vtkArrowSource.h>
#include <vtkCellLocator.h>
#include <vtkDoubleArray.h>
#include <vtkEllipseArcSource.h>
#include <vtkGlyph3DMapper.h>
#include <vtkLookupTable.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRegularPolygonSource.h>
#include <vtkRenderer.h>
#include <vtkSlicerROIRepresentation3D.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTubeFilter.h>

// MRML includes
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsROINode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSelectionNode.h"
#include "vtkMRMLViewNode.h"

#include <vtkCubeSource.h>
#include <vtkSphereSource.h>
#include <vtkParametricEllipsoid.h>
#include <vtkParametricFunctionSource.h>

vtkStandardNewMacro(vtkSlicerROIRepresentation3D);

//----------------------------------------------------------------------
vtkSlicerROIRepresentation3D::vtkSlicerROIRepresentation3D()
{
  this->ROISource = nullptr;
  this->ROITransformFilter->SetTransform(this->ROIToWorldTransform);
  this->ROIMapper->SetInputConnection(this->ROITransformFilter->GetOutputPort());
  this->ROIProperty->DeepCopy(this->GetControlPointsPipeline(Selected)->Property);
  this->ROIActor->SetMapper(this->ROIMapper);
  this->ROIActor->SetProperty(this->ROIProperty);

  this->ROIOccludedMapper->SetInputConnection(this->ROITransformFilter->GetOutputPort());
  this->ROIOccludedProperty->DeepCopy(this->ROIProperty);
  this->ROIOccludedActor->SetMapper(this->ROIOccludedMapper);
  this->ROIOccludedActor->SetProperty(this->ROIOccludedProperty);
  this->ROIOccludedActor->SetMapper(this->ROIOccludedMapper);
}

//----------------------------------------------------------------------
vtkSlicerROIRepresentation3D::~vtkSlicerROIRepresentation3D() = default;

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData /*=nullptr*/)
{
  Superclass::UpdateFromMRML(caller, event, callData);

  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode());
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!roiNode || !this->IsDisplayable() || !displayNode)
    {
    this->VisibilityOff();
    return;
    }

  switch (roiNode->GetROIType())
    {
    case vtkMRMLMarkupsROINode::Box:
      this->UpdateBoxFromMRML(roiNode);
      break;
    case vtkMRMLMarkupsROINode::Sphere:
      this->UpdateEllipseFromMRML(roiNode);
      break;
    default:
      this->ROIActor->SetVisibility(false);
      return;
    }

  this->ROIActor->SetVisibility(true);
  this->VisibilityOn();
  this->PickableOn();

  int controlPointType = Unselected;
  // TODO: Active
  controlPointType = this->GetAllControlPointsSelected() ? vtkSlicerMarkupsWidgetRepresentation::Selected : vtkSlicerMarkupsWidgetRepresentation::Unselected;

  double opacity = displayNode->GetOpacity();
  double fillOpacity = this->MarkupsDisplayNode->GetFillVisibility() ? displayNode->GetFillOpacity() : 0.0;
  this->ROIProperty->DeepCopy(this->GetControlPointsPipeline(controlPointType)->Property);
  this->ROIProperty->SetOpacity(opacity * fillOpacity);

  double occludedOpacity = displayNode->GetOccludedVisibility() ? fillOpacity * displayNode->GetOccludedOpacity() : 0.0;
  this->ROIOccludedProperty->DeepCopy(this->ROIProperty);
  this->ROIOccludedProperty->SetOpacity(opacity * fillOpacity * occludedOpacity);

  this->UpdateRelativeCoincidentTopologyOffsets(this->ROIMapper, this->ROIOccludedMapper);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::SetROISource(vtkPolyDataAlgorithm* roiSource)
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
void vtkSlicerROIRepresentation3D::UpdateBoxFromMRML(vtkMRMLMarkupsROINode* roiNode)
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

  double origin[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetOrigin(origin);

  this->ROIToWorldTransform->Identity();
  vtkNew<vtkMatrix4x4> axis;
  for (int i = 0; i < 3; ++i)
    {
    axis->SetElement(i, 0, roiNode->GetXAxis()[i]);
    axis->SetElement(i, 1, roiNode->GetYAxis()[i]);
    axis->SetElement(i, 2, roiNode->GetZAxis()[i]);
    axis->SetElement(i, 3, origin[i]);
    }
  this->ROIToWorldTransform->SetMatrix(axis);

  double sideLengths[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetSideLengths(sideLengths);
  cubeSource->SetXLength(sideLengths[0]);
  cubeSource->SetYLength(sideLengths[1]);
  cubeSource->SetZLength(sideLengths[2]);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::UpdateEllipseFromMRML(vtkMRMLMarkupsROINode* roiNode)
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

  double origin[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetOrigin(origin);

  this->ROIToWorldTransform->Identity();
  vtkNew<vtkMatrix4x4> axis;
  for (int i = 0; i < 3; ++i)
    {
    axis->SetElement(i, 0, roiNode->GetXAxis()[i]);
    axis->SetElement(i, 1, roiNode->GetYAxis()[i]);
    axis->SetElement(i, 2, roiNode->GetZAxis()[i]);
    axis->SetElement(i, 3, origin[i]);
    }
  this->ROIToWorldTransform->SetMatrix(axis);

  double sideLengths[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetSideLengths(sideLengths);
  ellipseFunction->SetXRadius(sideLengths[0] * 0.5);
  ellipseFunction->SetYRadius(sideLengths[1] * 0.5);
  ellipseFunction->SetZRadius(sideLengths[2] * 0.5);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::UpdateInteractionPipeline()
{
  // Final visibility handled by superclass in vtkSlicerMarkupsWidgetRepresentation
  Superclass::UpdateInteractionPipeline();
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::GetActors(vtkPropCollection *pc)
{
  this->ROIActor->GetActors(pc);
  this->ROIOccludedActor->GetActors(pc);
  this->Superclass::GetActors(pc);
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  this->ROIActor->ReleaseGraphicsResources(win);
  this->ROIOccludedActor->ReleaseGraphicsResources(win);
  this->Superclass::ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkSlicerROIRepresentation3D::RenderOverlay(vtkViewport *viewport)
{
  int count = this->Superclass::RenderOverlay(viewport);
  if (this->ROIActor->GetVisibility())
    {
    count += this->ROIActor->RenderOverlay(viewport);
    }
  if (this->ROIOccludedActor->GetVisibility())
    {
    count += this->ROIOccludedActor->RenderOverlay(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation3D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = this->Superclass::RenderOpaqueGeometry(viewport);
  if (this->ROIActor->GetVisibility())
    {
    count += this->ROIActor->RenderOpaqueGeometry(viewport);
    }
  if (this->ROIOccludedActor->GetVisibility())
    {
    count += this->ROIOccludedActor->RenderOpaqueGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerROIRepresentation3D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = this->Superclass::RenderTranslucentPolygonalGeometry(viewport);
  if (this->ROIActor->GetVisibility())
    {
    this->ROIActor->SetPropertyKeys(this->GetPropertyKeys());
    count += this->ROIActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->ROIOccludedActor->GetVisibility())
    {
    this->ROIOccludedActor->SetPropertyKeys(this->GetPropertyKeys());
    count += this->ROIOccludedActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkSlicerROIRepresentation3D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->ROIActor->GetVisibility() && this->ROIActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->ROIOccludedActor->GetVisibility() && this->ROIOccludedActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//----------------------------------------------------------------------
double *vtkSlicerROIRepresentation3D::GetBounds()
{
  vtkBoundingBox boundingBox;
  const std::vector<vtkProp*> actors({ this->ROIActor });
  this->AddActorsBounds(boundingBox, actors, Superclass::GetBounds());
  boundingBox.GetBounds(this->Bounds);
  return this->Bounds;
}

//----------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentNone;
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if ( !markupsNode || markupsNode->GetLocked() || markupsNode->GetNumberOfControlPoints() < 1
    || !interactionEventData )
    {
    return;
    }

  Superclass::CanInteract(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType != vtkMRMLMarkupsDisplayNode::ComponentNone)
    {
    return;
    }
}

//-----------------------------------------------------------------------------
void vtkSlicerROIRepresentation3D::PrintSelf(ostream& os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}
