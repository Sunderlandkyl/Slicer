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
#include "vtkCamera.h"
#include "vtkCellLocator.h"
#include "vtkDiscretizableColorTransferFunction.h"
#include "vtkGlyph2D.h"
#include "vtkLabelPlacementMapper.h"
#include "vtkLine.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPiecewiseFunction.h"
#include "vtkPlane.h"
#include "vtkPointData.h"
#include "vtkPointSetToLabelHierarchy.h"
#include "vtkPolyDataMapper2D.h"
#include "vtkProperty2D.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkMRMLInteractionWidgetRepresentation2D.h"
#include "vtkSphereSource.h"
#include "vtkStringArray.h"
#include "vtkTensorGlyph.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>

//----------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLInteractionWidgetRepresentation2D);

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation2D::vtkMRMLInteractionWidgetRepresentation2D()
{
  this->SlicePlane = vtkSmartPointer<vtkPlane>::New();
  this->WorldToSliceTransform = vtkSmartPointer<vtkTransform>::New();
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation2D::~vtkMRMLInteractionWidgetRepresentation2D() = default;

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::GetSliceToWorldCoordinates(const double slicePos[2],
                                                                        double worldPos[3])
{
  vtkMRMLSliceNode *sliceNode = this->GetSliceNode();
  if (!this->Renderer || !sliceNode)
    {
    return;
    }

  double xyzw[4] =
  {
    slicePos[0] - this->Renderer->GetOrigin()[0],
    slicePos[1] - this->Renderer->GetOrigin()[1],
    0.0,
    1.0
  };
  double rasw[4] = { 0.0, 0.0, 0.0, 1.0 };

  this->GetSliceNode()->GetXYToRAS()->MultiplyPoint(xyzw, rasw);

  worldPos[0] = rasw[0]/rasw[3];
  worldPos[1] = rasw[1]/rasw[3];
  worldPos[2] = rasw[2]/rasw[3];
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::GetWorldToSliceCoordinates(const double worldPos[3], double slicePos[2])
{
  vtkMRMLSliceNode *sliceNode = this->GetSliceNode();
  if (!sliceNode)
    {
    return;
    }

  double sliceCoordinates[4], worldCoordinates[4];
  worldCoordinates[0] = worldPos[0];
  worldCoordinates[1] = worldPos[1];
  worldCoordinates[2] = worldPos[2];
  worldCoordinates[3] = 1.0;

  vtkNew<vtkMatrix4x4> rasToxyMatrix;
  sliceNode->GetXYToRAS()->Invert(sliceNode->GetXYToRAS(), rasToxyMatrix.GetPointer());

  rasToxyMatrix->MultiplyPoint(worldCoordinates, sliceCoordinates);

  slicePos[0] = sliceCoordinates[0];
  slicePos[1] = sliceCoordinates[1];
}

//----------------------------------------------------------------------
vtkMRMLSliceNode *vtkMRMLInteractionWidgetRepresentation2D::GetSliceNode()
{
  return vtkMRMLSliceNode::SafeDownCast(this->ViewNode);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData/*=nullptr*/)
{
  this->UpdatePlaneFromSliceNode();
  Superclass::UpdateFromMRML(caller, event, callData);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = vtkMRMLDisplayNode::InteractionNone;
  vtkMRMLSliceNode *sliceNode = this->GetSliceNode();
  vtkMRMLDisplayNode* displayNode = this->GetDisplayNode();
  if (!sliceNode || !displayNode || !this->GetVisibility() || !interactionEventData)
    {
    return;
    }

  const int* displayPosition = interactionEventData->GetDisplayPosition();
  double displayPosition3[3] = { static_cast<double>(displayPosition[0]), static_cast<double>(displayPosition[1]), 0.0 };

  this->UpdateHandleSize();
  double maxPickingDistanceFromControlPoint2 = this->GetMaximumHandlePickingDistance2();

  closestDistance2 = VTK_DOUBLE_MAX; // in display coordinate system
  foundComponentIndex = -1;

  // We can interact with the handle if the mouse is hovering over one of the handles (translation or rotation), in display coordinates.
  this->CanInteractWithHandles(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType != vtkMRMLDisplayNode::InteractionNone)
    {
    // if mouse is near a handle then select that (ignore the line + control points)
    return;
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::CanInteractWithHandles(
  vtkMRMLInteractionEventData* interactionEventData,
  int& foundComponentType, int& foundComponentIndex, double& closestDistance2)
{
  if (!this->Pipeline || !this->Pipeline->Actor->GetVisibility())
    {
    return;
    }

  double maxPickingDistanceFromControlPoint2 = this->GetMaximumHandlePickingDistance2();

  const int* displayPosition = interactionEventData->GetDisplayPosition();
  double displayPosition3[3] = { static_cast<double>(displayPosition[0]), static_cast<double>(displayPosition[1]), 0.0 };

  double handleDisplayPos[4] = { 0.0, 0.0, 0.0, 1.0 };

  vtkMRMLSliceNode* sliceNode = this->GetSliceNode();
  vtkNew<vtkMatrix4x4> rasToxyMatrix;
  vtkMatrix4x4::Invert(sliceNode->GetXYToRAS(), rasToxyMatrix);

  bool handlePicked = false;
  HandleInfoList handleInfoList = this->GetHandleInfoList();
  for (HandleInfo handleInfo : handleInfoList)
    {
    if (!handleInfo.IsVisible())
      {
      continue;
      }
    double* handleWorldPos = handleInfo.PositionWorld;
    rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
    handleDisplayPos[2] = displayPosition3[2]; // Handles are always projected
    double dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
    if (dist2 < maxPickingDistanceFromControlPoint2 && dist2 < closestDistance2)
      {
      closestDistance2 = dist2;
      foundComponentType = handleInfo.ComponentType;
      foundComponentIndex = handleInfo.Index;
      handlePicked = true;
      }
    }

  if (!handlePicked)
    {
    // Detect translation handle shaft
    for (HandleInfo handleInfo : handleInfoList)
      {
      if (!handleInfo.IsVisible() || handleInfo.ComponentType != vtkMRMLDisplayNode::InteractionTranslationHandle)
        {
        continue;
        }

      double* handleWorldPos = handleInfo.PositionWorld;
      rasToxyMatrix->MultiplyPoint(handleWorldPos, handleDisplayPos);
      handleDisplayPos[2] = displayPosition3[2]; // Handles are always projected

      double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
      this->GetInteractionHandleOriginWorld(originWorldPos);
      double originDisplayPos[4] = { 0.0 };
      rasToxyMatrix->MultiplyPoint(originWorldPos, originDisplayPos);
      originDisplayPos[2] = displayPosition3[2]; // Handles are always projected

      double t = 0;
      double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos, handleDisplayPos, t);
      double lineDistance2 = lineDistance * lineDistance;
      if (lineDistance2 < maxPickingDistanceFromControlPoint2 / 2.0 && lineDistance2 < closestDistance2)
        {
        closestDistance2 = lineDistance2;
        foundComponentType = handleInfo.ComponentType;
        foundComponentIndex = handleInfo.Index;
        }
      }
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::GetActors(vtkPropCollection *pc)
{
  Superclass::GetActors(pc);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  Superclass::ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation2D::RenderOverlay(vtkViewport *viewport)
{
  int count = Superclass::RenderOverlay(viewport);
  return count;
}

//-----------------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation2D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderOpaqueGeometry(viewport);
  return count;
}

//-----------------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation2D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderTranslucentPolygonalGeometry(viewport);
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkMRMLInteractionWidgetRepresentation2D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::PrintSelf(ostream& os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
/// Convert world to display coordinates
void vtkMRMLInteractionWidgetRepresentation2D::GetWorldToDisplayCoordinates(double r, double a, double s, double * displayCoordinates)
{
  vtkMRMLSliceNode *sliceNode = this->GetSliceNode();
  if (!sliceNode)
    {
    vtkErrorMacro("GetWorldToDisplayCoordinates: no slice node!");
    return;
    }

  // we will get the transformation matrix to convert world coordinates to the display coordinates of the specific sliceNode

  vtkMatrix4x4 * xyToRasMatrix = sliceNode->GetXYToRAS();
  vtkNew<vtkMatrix4x4> rasToXyMatrix;

  // we need to invert this matrix
  xyToRasMatrix->Invert(xyToRasMatrix, rasToXyMatrix.GetPointer());

  double worldCoordinates[4];
  worldCoordinates[0] = r;
  worldCoordinates[1] = a;
  worldCoordinates[2] = s;
  worldCoordinates[3] = 1;

  rasToXyMatrix->MultiplyPoint(worldCoordinates, displayCoordinates);
  xyToRasMatrix = nullptr;
}

//---------------------------------------------------------------------------
// Convert world to display coordinates
void vtkMRMLInteractionWidgetRepresentation2D::GetWorldToDisplayCoordinates(double * worldCoordinates, double * displayCoordinates)
{
  if (worldCoordinates == nullptr)
    {
    return;
    }

  this->GetWorldToDisplayCoordinates(worldCoordinates[0], worldCoordinates[1], worldCoordinates[2], displayCoordinates);
}


//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::UpdatePlaneFromSliceNode()
{
  vtkMatrix4x4* sliceXYToRAS = this->GetSliceNode()->GetXYToRAS();

  // Update transformation to slice
  vtkNew<vtkMatrix4x4> rasToSliceXY;
  vtkMatrix4x4::Invert(sliceXYToRAS, rasToSliceXY.GetPointer());
  // Project all points to the slice plane (slice Z coordinate = 0)
  rasToSliceXY->SetElement(2, 0, 0);
  rasToSliceXY->SetElement(2, 1, 0);
  rasToSliceXY->SetElement(2, 2, 0);
  this->WorldToSliceTransform->SetMatrix(rasToSliceXY.GetPointer());

  // Update slice plane (for distance computation)
  double normal[3];
  double origin[3];
  const double planeOrientation = 1.0; // +/-1: orientation of the normal
  for (int i = 0; i < 3; i++)
    {
    normal[i] = planeOrientation * sliceXYToRAS->GetElement(i, 2);
    origin[i] = sliceXYToRAS->GetElement(i, 3);
    }
  vtkMath::Normalize(normal);
  this->SlicePlane->SetNormal(normal);
  this->SlicePlane->SetOrigin(origin);
  this->SlicePlane->Modified();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::UpdateViewScaleFactor()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera() || !this->GetSliceNode())
    {
    return;
    }

  int* screenSize = this->Renderer->GetRenderWindow()->GetScreenSize();
  this->ScreenSizePixel = sqrt(screenSize[0] * screenSize[0] + screenSize[1] * screenSize[1]);

  vtkMatrix4x4* xyToSlice = this->GetSliceNode()->GetXYToSlice();
  this->ViewScaleFactorMmPerPixel = sqrt(xyToSlice->GetElement(0, 1) * xyToSlice->GetElement(0, 1)
    + xyToSlice->GetElement(1, 1) * xyToSlice->GetElement(1, 1));
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::UpdateHandleSize()
{
  // Since we use parallel camera projection and the camera scale is 1.0,
  // the renderer coordinate system is the same as the display coordinate system, therefore
  // InteractionSize is specified in pixels.
  if (!this->DisplayNode->GetInteractionSizeAbsolute())
    {
    // relative
    this->InteractionSize = this->ScreenSizePixel * this->ScreenScaleFactor * this->DisplayNode->GetInteractionScale() / 100.0;
    }
  else
    {
    // absolute
    this->InteractionSize = this->DisplayNode->GetInteractionSize() / this->ViewScaleFactorMmPerPixel;
    }
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation2D::GetMaximumHandlePickingDistance2()
{
  double maximumHandlePickingDistance = this->InteractionSize / 2.0 + this->PickingTolerance * this->ScreenScaleFactor;
  return maximumHandlePickingDistance * maximumHandlePickingDistance;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::SetupInteractionPipeline()
{
  this->Pipeline = new InteractionPipeline2D();
  this->InitializePipeline();
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::UpdateInteractionPipeline()
{
  InteractionPipeline2D* interactionPipeline = dynamic_cast<InteractionPipeline2D*>(this->Pipeline);
  if (!interactionPipeline)
    {
    return;
    }
  interactionPipeline->WorldToSliceTransformFilter->SetTransform(this->WorldToSliceTransform);
  // Final visibility handled by superclass in vtkMRMLInteractionWidgetRepresentation
  Superclass::UpdateInteractionPipeline();
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation2D::InteractionPipeline2D::InteractionPipeline2D()
{
  this->WorldToSliceTransformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  this->WorldToSliceTransformFilter->SetTransform(vtkNew<vtkTransform>());
  this->WorldToSliceTransformFilter->SetInputConnection(this->HandleToWorldTransformFilter->GetOutputPort());

  this->Mapper->SetInputConnection(this->WorldToSliceTransformFilter->GetOutputPort());
  this->Mapper->SetTransformCoordinate(nullptr);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation2D::GetViewPlaneNormal(double viewPlaneNormal[3])
{
  if (!viewPlaneNormal)
    {
    return;
    }

  double viewPlaneNormal4[4] = { 0, 0, 1, 0 };
  vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(this->GetViewNode());
  if (sliceNode)
    {
    sliceNode->GetSliceToRAS()->MultiplyPoint(viewPlaneNormal4, viewPlaneNormal4);
    }
  viewPlaneNormal[0] = viewPlaneNormal4[0];
  viewPlaneNormal[1] = viewPlaneNormal4[1];
  viewPlaneNormal[2] = viewPlaneNormal4[2];
}
