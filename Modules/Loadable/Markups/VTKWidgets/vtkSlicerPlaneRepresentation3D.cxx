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
#include <vtkAppendPolyData.h>
#include <vtkArcSource.h>
#include <vtkArrowSource.h>
#include <vtkCellLocator.h>
#include <vtkDoubleArray.h>
#include <vtkEllipseArcSource.h>
#include <vtkGlyph3DMapper.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkPointData.h>
#include <vtkProperty.h>
#include <vtkRegularPolygonSource.h>
#include <vtkRenderer.h>
#include <vtkSlicerPlaneRepresentation3D.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>

// MRML includes
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsPlaneNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSelectionNode.h"

vtkStandardNewMacro(vtkSlicerPlaneRepresentation3D);

//----------------------------------------------------------------------
vtkSlicerPlaneRepresentation3D::vtkSlicerPlaneRepresentation3D()
{
  this->PlaneMapper->SetInputData(vtkNew<vtkPolyData>());

  this->PlaneActor->SetMapper(this->PlaneMapper);
  this->PlaneActor->SetProperty(this->GetControlPointsPipeline(Unselected)->Property);

  this->ArrowFilter->SetTipResolution(50);

  this->ArrowMapper->SetOrientationModeToDirection();
  this->ArrowMapper->SetOrientationArray(vtkDataObject::FIELD_ASSOCIATION_POINTS);
  this->ArrowMapper->SetSourceConnection(this->ArrowFilter->GetOutputPort());
  this->ArrowMapper->SetScalarVisibility(false);

  this->ArrowActor->SetMapper(this->ArrowMapper);
  this->ArrowActor->SetProperty(this->GetControlPointsPipeline(Unselected)->Property);

  this->XArcFilter->GeneratePolygonOff();
  this->XArcFilter->SetNumberOfSides(180);
  this->XArcMapper->SetInputConnection(this->XArcFilter->GetOutputPort());
  this->XArcActor->SetMapper(this->XArcMapper);
  this->XArcActor->GetProperty()->SetColor(1, 0, 0);
  this->XArcActor->GetProperty()->SetLineWidth(5);

  this->YArcFilter->GeneratePolygonOff();
  this->YArcFilter->SetNumberOfSides(180);
  this->YArcMapper->SetInputConnection(this->YArcFilter->GetOutputPort());
  this->YArcActor->SetMapper(this->YArcMapper);
  this->YArcActor->GetProperty()->SetColor(0, 1, 0);
  this->YArcActor->GetProperty()->SetLineWidth(5);

  this->ZArcFilter->GeneratePolygonOff();
  this->ZArcFilter->SetNumberOfSides(180);
  this->ZArcMapper->SetInputConnection(this->ZArcFilter->GetOutputPort());
  this->ZArcActor->SetMapper(this->ZArcMapper);
  this->ZArcActor->GetProperty()->SetColor(0, 0, 1);
  this->ZArcActor->GetProperty()->SetLineWidth(5);

  this->LabelFormat = "%-#6.3g";
}

//----------------------------------------------------------------------
vtkSlicerPlaneRepresentation3D::~vtkSlicerPlaneRepresentation3D()
= default;

//----------------------------------------------------------------------
bool vtkSlicerPlaneRepresentation3D::GetTransformationReferencePoint(double referencePointWorld[3])
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || markupsNode->GetNumberOfControlPoints() < 2)
    {
    return false;
    }
  markupsNode->GetNthControlPointPositionWorld(1, referencePointWorld);
  return true;
}

//----------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::BuildPlane()
{
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode || markupsNode->GetNumberOfControlPoints() != 3)
    {
    this->PlaneActor->SetVisibility(false);
    return;
    }

  double x[3], y[3], z[3] = { 0 };
  markupsNode->GetPlaneAxesWorld(x, y, z);

  if (vtkMath::Norm(x) <= 0.0001 || vtkMath::Norm(y) <= 0.0001 || vtkMath::Norm(z) <= 0.0001)
    {
    this->PlaneActor->SetVisibility(false);
    return;
    }

  vtkNew<vtkPoints> points;

  double origin[3] = { 0.0 };
  markupsNode->GetOriginWorld(origin);
  points->InsertNextPoint(origin);

  vtkNew<vtkDoubleArray> directionArray;
  directionArray->SetNumberOfComponents(3);
  directionArray->InsertNextTuple3(z[0], z[1], z[2]);
  directionArray->SetName("direction");

  vtkNew<vtkPolyData> polyData;
  polyData->SetPoints(points);
  polyData->GetPointData()->SetScalars(directionArray);

  this->ArrowGlypher->SetInputData(polyData);

  // Update the plane
  double size[2] = { 0 };
  markupsNode->GetSize(size);
  vtkMath::MultiplyScalar(x, size[0]/2);
  vtkMath::MultiplyScalar(y, size[1]/2);

  double planePoint1[3] = { 0 };
  vtkMath::Subtract(origin, x, planePoint1);
  vtkMath::Subtract(planePoint1, y, planePoint1);

  double planePoint2[3] = { 0 };
  vtkMath::Subtract(origin, x, planePoint2);
  vtkMath::Add(planePoint2, y, planePoint2);

  double planePoint3[3] = { 0 };
  vtkMath::Add(origin, x, planePoint3);
  vtkMath::Subtract(planePoint3, y, planePoint3);

  this->PlaneFilter->SetOrigin(planePoint1);
  this->PlaneFilter->SetPoint1(planePoint2);
  this->PlaneFilter->SetPoint2(planePoint3);
}

//----------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::BuildArcs()
{
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode || markupsNode->GetNumberOfControlPoints() != 3)
    {
    this->PlaneMapper->SetInputData(vtkNew<vtkPolyData>());
    this->ArrowMapper->SetInputData(vtkNew<vtkPolyData>());
    return;
    }

  bool showArcs = markupsNode->GetNumberOfControlPoints() >= 3 && markupsNode->GetSelected();

  vtkMRMLScene* scene = markupsNode->GetScene();
  if (showArcs && scene)
    {
    vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(scene->GetFirstNodeByClass("vtkMRMLSelectionNode"));
    if (selectionNode)
      {
      showArcs = selectionNode->IsOnlyNodeSelected(markupsNode);
      }
    }

  this->XArcActor->SetVisibility(showArcs);
  this->YArcActor->SetVisibility(showArcs);
  this->ZArcActor->SetVisibility(showArcs);
  if (!showArcs)
    {
    return;
    }

  double origin[3] = { 0 };
  markupsNode->GetOriginWorld(origin);
  this->XArcFilter->SetCenter(origin);
  this->YArcFilter->SetCenter(origin);
  this->ZArcFilter->SetCenter(origin);

  double x[3], y[3], z[3] = { 0 };
  markupsNode->GetPlaneAxes(x, y, z);
  this->XArcFilter->SetNormal(x);
  this->YArcFilter->SetNormal(y);
  this->ZArcFilter->SetNormal(z);

  this->XArcFilter->SetRadius(this->ControlPointSize * 2);
  this->YArcFilter->SetRadius(this->ControlPointSize * 2);
  this->ZArcFilter->SetRadius(this->ControlPointSize * 2);

  //double xRadiusVector[3], yRadiusVector[3], zRadiusVector[3] = { 0 };
  //vtkMath::Add(y, z, xRadiusVector);
  //vtkMath::Add(x, z, yRadiusVector);
  //vtkMath::Add(x, y, zRadiusVector);
  //this->XArcFilter->SetMajorRadiusVector(xRadiusVector);
  //this->YArcFilter->SetMajorRadiusVector(yRadiusVector);
  //this->ZArcFilter->SetMajorRadiusVector(zRadiusVector);
}

//----------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData /*=nullptr*/)
{
  Superclass::UpdateFromMRML(caller, event, callData);

  this->NeedToRenderOn();

  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  if (!markupsNode || !this->MarkupsDisplayNode
    || !this->MarkupsDisplayNode->GetVisibility()
    || !this->MarkupsDisplayNode->IsDisplayableInView(this->ViewNode->GetID())
    )
    {
    this->VisibilityOff();
    return;
    }

  this->VisibilityOn();
  this->PickableOn();

  // Update plane geometry
  this->BuildPlane();
  this->BuildArcs();

  // Update plane display properties
  this->PlaneActor->SetVisibility(markupsNode->GetNumberOfControlPoints() >= 3);

  this->TextActor->SetVisibility(this->MarkupsDisplayNode->GetPropertiesLabelVisibility());

  this->UpdateRelativeCoincidentTopologyOffsets(this->PlaneMapper);

  int controlPointType = Active;
  if (this->MarkupsDisplayNode->GetActiveComponentType() != vtkMRMLMarkupsDisplayNode::ComponentPlane)
    {
    controlPointType = this->GetAllControlPointsSelected() ? Selected : Unselected;
    }
  this->PlaneActor->SetProperty(this->GetControlPointsPipeline(controlPointType)->Property);
  this->TextActor->SetTextProperty(this->GetControlPointsPipeline(controlPointType)->TextProperty);
}

//----------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::GetActors(vtkPropCollection *pc)
{
  this->Superclass::GetActors(pc);
  this->PlaneActor->GetActors(pc);
  this->TextActor->GetActors(pc);
  this->XArcActor->GetActors(pc);
  this->YArcActor->GetActors(pc);
  this->ZArcActor->GetActors(pc);
}

//----------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  this->Superclass::ReleaseGraphicsResources(win);
  this->PlaneActor->ReleaseGraphicsResources(win);
  this->TextActor->ReleaseGraphicsResources(win);
  this->XArcActor->ReleaseGraphicsResources(win);
  this->YArcActor->ReleaseGraphicsResources(win);
  this->ZArcActor->ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkSlicerPlaneRepresentation3D::RenderOverlay(vtkViewport *viewport)
{
  int count = this->Superclass::RenderOverlay(viewport);
  if (this->PlaneActor->GetVisibility())
    {
    count += this->PlaneActor->RenderOverlay(viewport);
    }
  if (this->TextActor->GetVisibility())
    {
    count += this->TextActor->RenderOverlay(viewport);
    }
  if (this->XArcActor->GetVisibility())
    {
    count += this->XArcActor->RenderOverlay(viewport);
    }
  if (this->YArcActor->GetVisibility())
    {
    count += this->YArcActor->RenderOverlay(viewport);
    }
  if (this->ZArcActor->GetVisibility())
    {
    count += this->ZArcActor->RenderOverlay(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerPlaneRepresentation3D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = this->Superclass::RenderOpaqueGeometry(viewport);
  if (this->PlaneActor->GetVisibility())
    {
    this->ArrowGlypher->SetScaleFactor(this->ControlPointSize * 3);
    count += this->PlaneActor->RenderOpaqueGeometry(viewport);
    }
  if (this->TextActor->GetVisibility())
    {
    count += this->TextActor->RenderOpaqueGeometry(viewport);
    }
  if (this->XArcActor->GetVisibility())
    {
    this->XArcFilter->SetRadius(this->ControlPointSize * 2);
    count += this->XArcActor->RenderOpaqueGeometry(viewport);
    }
  if (this->YArcActor->GetVisibility())
    {
    this->YArcFilter->SetRadius(this->ControlPointSize * 2);
    count += this->YArcActor->RenderOpaqueGeometry(viewport);
    }
  if (this->ZArcActor->GetVisibility())
    {
    this->ZArcFilter->SetRadius(this->ControlPointSize * 2);
    count += this->ZArcActor->RenderOpaqueGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
int vtkSlicerPlaneRepresentation3D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = this->Superclass::RenderTranslucentPolygonalGeometry(viewport);
  if (this->PlaneActor->GetVisibility())
    {
    // The internal actor needs to share property keys.
    // This ensures the mapper state is consistent and allows depth peeling to work as expected.
    this->PlaneActor->SetPropertyKeys(this->GetPropertyKeys());
    count += this->PlaneActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->TextActor->GetVisibility())
    {
    count += this->TextActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->XArcActor->GetVisibility())
    {
    count += this->XArcActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->YArcActor->GetVisibility())
    {
    count += this->YArcActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  if (this->ZArcActor->GetVisibility())
    {
    count += this->ZArcActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkSlicerPlaneRepresentation3D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->PlaneActor->GetVisibility() && this->PlaneActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->TextActor->GetVisibility() && this->TextActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->XArcActor->GetVisibility() && this->XArcActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->YArcActor->GetVisibility() && this->YArcActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  if (this->ZArcActor->GetVisibility() && this->ZArcActor->HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//----------------------------------------------------------------------
double *vtkSlicerPlaneRepresentation3D::GetBounds()
{
  vtkBoundingBox boundingBox;
  const std::vector<vtkProp*> actors({ this->PlaneActor });
  this->AddActorsBounds(boundingBox, actors, Superclass::GetBounds());
  boundingBox.GetBounds(this->Bounds);
  return this->Bounds;
}

//----------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::CanInteract(
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

  this->CanInteractWithHandles(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  this->CanInteractWithPlane(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType != vtkMRMLMarkupsDisplayNode::ComponentNone)
    {
    return;
    }

  this->CanInteractWithPlane(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
}

//-----------------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::CanInteractWithHandles(
  vtkMRMLInteractionEventData* interactionEventData,
  int& foundComponentType, int& foundComponentIndex, double& closestDistance2)
{
  /// TODO: Implement handles in a better way

  //// Create the tree
  //vtkSmartPointer<vtkCellLocator> cellLocator =
  //  vtkSmartPointer<vtkCellLocator>::New();
  //this->XArcFilter->Update();
  //if (this->XArcFilter->GetOutput() && this->XArcFilter->GetOutput()->GetNumberOfPoints() == 0)
  //  {
  //  return;
  //  }

  //cellLocator->SetDataSet(this->XArcFilter->GetOutput());
  //cellLocator->BuildLocator();

  //const double* worldPosition = interactionEventData->GetWorldPosition();
  //double closestPoint[3];//the coordinates of the closest point will be returned here
  //double distance2; //the squared distance to the closest point will be returned here
  //vtkIdType cellId; //the cell id of the cell containing the closest point will be returned here
  //int subId; //this is rarely used (in triangle strips only, I believe)
  //cellLocator->FindClosestPoint(worldPosition, closestPoint, cellId, subId, distance2);

  //double toleranceWorld = this->ControlPointSize / 2;
  //if (distance2 < toleranceWorld)
  //  {
  //  closestDistance2 = distance2;
  //  foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentPlane;
  //  foundComponentIndex = 0;
  //  }
}

//-----------------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::CanInteractWithPlane(
  vtkMRMLInteractionEventData* interactionEventData,
  int& foundComponentType, int& foundComponentIndex, double& closestDistance2)
{
  // Create the tree
  vtkSmartPointer<vtkCellLocator> cellLocator =
    vtkSmartPointer<vtkCellLocator>::New();
  this->PlaneFilter->Update();
  if (this->PlaneFilter->GetOutput() && this->PlaneFilter->GetOutput()->GetNumberOfPoints() == 0)
    {
    return;
    }

  cellLocator->SetDataSet(this->PlaneFilter->GetOutput());
  cellLocator->BuildLocator();

  const double* worldPosition = interactionEventData->GetWorldPosition();
  double closestPoint[3];//the coordinates of the closest point will be returned here
  double distance2; //the squared distance to the closest point will be returned here
  vtkIdType cellId; //the cell id of the cell containing the closest point will be returned here
  int subId; //this is rarely used (in triangle strips only, I believe)
  cellLocator->FindClosestPoint(worldPosition, closestPoint, cellId, subId, distance2);

  double toleranceWorld = this->ControlPointSize / 2;
  if (distance2 < toleranceWorld)
    {
    closestDistance2 = distance2;
    foundComponentType = vtkMRMLMarkupsDisplayNode::ComponentPlane;
    foundComponentIndex = 0;
    }
}

//-----------------------------------------------------------------------------
void vtkSlicerPlaneRepresentation3D::PrintSelf(ostream& os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);

  if (this->PlaneActor)
    {
    os << indent << "Plane Visibility: " << this->PlaneActor->GetVisibility() << "\n";
    }
  else
    {
    os << indent << "Plane Visibility: (none)\n";
    }
  if (this->TextActor)
    {
    os << indent << "Text Visibility: " << this->TextActor->GetVisibility() << "\n";
    }
  else
    {
    os << indent << "Text Visibility: (none)\n";
    }
}
