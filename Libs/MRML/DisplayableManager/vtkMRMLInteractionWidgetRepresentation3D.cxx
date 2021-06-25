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
#include "vtkCallbackCommand.h"
#include "vtkCamera.h"
#include "vtkCellPicker.h"
#include "vtkLabelPlacementMapper.h"
#include "vtkLine.h"
#include "vtkFloatArray.h"
#include "vtkGlyph3DMapper.h"

#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPointSetToLabelHierarchy.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"

#include "vtkMRMLInteractionWidgetRepresentation3D.h"
#include "vtkSphereSource.h"
#include "vtkStringArray.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"

// MRML includes
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLViewNode.h>

//----------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLInteractionWidgetRepresentation3D);

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation3D::vtkMRMLInteractionWidgetRepresentation3D()
{
  this->InteractionSize = 10; // will be set from GlyphScale

  this->AccuratePicker = vtkSmartPointer<vtkCellPicker>::New();
  this->AccuratePicker->SetTolerance(.005);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidgetRepresentation3D::~vtkMRMLInteractionWidgetRepresentation3D() = default;

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::CanInteract(
  vtkMRMLInteractionEventData* interactionEventData,
  int &foundComponentType, int &foundComponentIndex, double &closestDistance2)
{
  foundComponentType = vtkMRMLDisplayNode::InteractionNone;
  vtkMRMLDisplayNode* displayNode = this->GetDisplayNode();
  if ( !displayNode || !this->GetVisibility() || !interactionEventData )
    {
    return;
    }

  double displayPosition3[3] = { 0.0, 0.0, 0.0 };
  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  if (interactionEventData->IsDisplayPositionValid())
    {
    const int* displayPosition = interactionEventData->GetDisplayPosition();
    displayPosition3[0] = static_cast<double>(displayPosition[0]);
    displayPosition3[1] = static_cast<double>(displayPosition[1]);
    }
  else if (!interactionEventData->IsWorldPositionValid())
    {
    return;
    }

  closestDistance2 = VTK_DOUBLE_MAX; // in display coordinate system (phyisical in case of virtual reality renderer)
  foundComponentIndex = -1;

  // We can interact with the handle if the mouse is hovering over one of the handles (translation or rotation), in display coordinates.
  // If display coordinates for the interaction event are not valid, world coordinates will be checked instead.
  this->CanInteractWithHandles(interactionEventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType != vtkMRMLDisplayNode::InteractionNone)
    {
    // if mouse is near a handle then select that (ignore the line + control points)
    return;
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::CanInteractWithHandles(
  vtkMRMLInteractionEventData* interactionEventData,
  int& foundComponentType, int& foundComponentIndex, double& closestDistance2)
{
  if (!this->Pipeline || !this->Pipeline->Actor->GetVisibility())
    {
    return;
    }

  double displayPosition3[3] = { 0.0, 0.0, 0.0 };
  // Display position is valid in case of desktop interactions. Otherwise it is a 3D only context such as
  // virtual reality, and then we expect a valid world position in the absence of display position.
  if (interactionEventData->IsDisplayPositionValid())
    {
    const int* displayPosition = interactionEventData->GetDisplayPosition();
    displayPosition3[0] = static_cast<double>(displayPosition[0]);
    displayPosition3[1] = static_cast<double>(displayPosition[1]);
    }
  else if (!interactionEventData->IsWorldPositionValid())
    {
    return;
    }

  bool handlePicked = false;
  HandleInfoList handleInfoList = this->GetHandleInfoList();
  for (HandleInfo handleInfo : handleInfoList)
    {
    if (!handleInfo.IsVisible())
      {
      continue;
      }

    double* handleWorldPos = handleInfo.PositionWorld;
    double handleDisplayPos[3] = { 0 };

    if (interactionEventData->IsDisplayPositionValid())
      {
      double pixelTolerance = this->InteractionSize / 2.0 / this->GetViewScaleFactorAtPosition(handleWorldPos)
        + this->PickingTolerance * this->ScreenScaleFactor;
      this->Renderer->SetWorldPoint(handleWorldPos);
      this->Renderer->WorldToDisplay();
      this->Renderer->GetDisplayPoint(handleDisplayPos);
      handleDisplayPos[2] = 0.0;
      double dist2 = vtkMath::Distance2BetweenPoints(handleDisplayPos, displayPosition3);
      if (dist2 < pixelTolerance * pixelTolerance && dist2 < closestDistance2)
        {
        closestDistance2 = dist2;
        foundComponentType = handleInfo.ComponentType;
        foundComponentIndex = handleInfo.Index;
        handlePicked = true;
        }
      }
    else
      {
      const double* worldPosition = interactionEventData->GetWorldPosition();
      double worldTolerance = this->InteractionSize / 2.0 +
        this->PickingTolerance / interactionEventData->GetWorldToPhysicalScale();
      double dist2 = vtkMath::Distance2BetweenPoints(handleWorldPos, worldPosition);
      if (dist2 < worldTolerance * worldTolerance && dist2 < closestDistance2)
        {
        closestDistance2 = dist2;
        foundComponentType = handleInfo.ComponentType;
        foundComponentIndex = handleInfo.Index;
        }
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
      double handleDisplayPos[3] = { 0 };

      if (interactionEventData->IsDisplayPositionValid())
        {
        double pixelTolerance = this->InteractionSize / 2.0 / this->GetViewScaleFactorAtPosition(handleWorldPos)
          + this->PickingTolerance * this->ScreenScaleFactor;
        this->Renderer->SetWorldPoint(handleWorldPos);
        this->Renderer->WorldToDisplay();
        this->Renderer->GetDisplayPoint(handleDisplayPos);
        handleDisplayPos[2] = 0.0;

        double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
        this->GetInteractionHandleOriginWorld(originWorldPos);
        double originDisplayPos[4] = { 0.0 };
        this->Renderer->SetWorldPoint(originWorldPos);
        this->Renderer->WorldToDisplay();
        this->Renderer->GetDisplayPoint(originDisplayPos);
        originDisplayPos[2] = displayPosition3[2]; // Handles are always projected
        double t = 0;
        double lineDistance = vtkLine::DistanceToLine(displayPosition3, originDisplayPos, handleDisplayPos, t);
        double lineDistance2 = lineDistance * lineDistance;
        if (lineDistance < pixelTolerance && lineDistance2 < closestDistance2)
          {
          closestDistance2 = lineDistance2;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          }
        }
      else
        {
        const double* worldPosition = interactionEventData->GetWorldPosition();
        double worldTolerance = this->InteractionSize / 2.0 +
          this->PickingTolerance / interactionEventData->GetWorldToPhysicalScale();
        double originWorldPos[4] = { 0.0, 0.0, 0.0, 1.0 };
        this->GetInteractionHandleOriginWorld(originWorldPos);
        double t;
        double lineDistance = vtkLine::DistanceToLine(worldPosition, originWorldPos, handleWorldPos, t);
        if (lineDistance < worldTolerance && lineDistance < closestDistance2)
          {
          closestDistance2 = lineDistance;
          foundComponentType = handleInfo.ComponentType;
          foundComponentIndex = handleInfo.Index;
          }
        }
      }
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData /*=nullptr*/)
{
 Superclass::UpdateFromMRML(caller, event, callData);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::GetActors(vtkPropCollection *pc)
{
  Superclass::GetActors(pc);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::ReleaseGraphicsResources(
  vtkWindow *win)
{
  Superclass::ReleaseGraphicsResources(win);
}

//----------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation3D::RenderOverlay(vtkViewport *viewport)
{
  int count = Superclass::RenderOverlay(viewport);
  return count;
}

//-----------------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation3D::RenderOpaqueGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderOpaqueGeometry(viewport);
  return count;
}

//-----------------------------------------------------------------------------
int vtkMRMLInteractionWidgetRepresentation3D::RenderTranslucentPolygonalGeometry(
  vtkViewport *viewport)
{
  int count = Superclass::RenderTranslucentPolygonalGeometry(viewport);
  return count;
}

//-----------------------------------------------------------------------------
vtkTypeBool vtkMRMLInteractionWidgetRepresentation3D::HasTranslucentPolygonalGeometry()
{
  if (this->Superclass::HasTranslucentPolygonalGeometry())
    {
    return true;
    }
  return false;
}

//-----------------------------------------------------------------------------
double *vtkMRMLInteractionWidgetRepresentation3D::GetBounds()
{
  return Superclass::GetBounds();
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::PrintSelf(ostream& os,
                                                      vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::SetRenderer(vtkRenderer *ren)
{
  Superclass::SetRenderer(ren);
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionWidgetRepresentation3D::AccuratePick(int x, int y, double pickPoint[3])
{
  if (!this->AccuratePicker->Pick(x, y, 0, this->Renderer))
    {
    return false;
    }

  vtkPoints* pickPositions = this->AccuratePicker->GetPickedPositions();
  vtkIdType numberOfPickedPositions = pickPositions->GetNumberOfPoints();
  if (numberOfPickedPositions<1)
    {
    return false;
    }

  // There may be multiple picked positions, choose the one closest to the camera
  double cameraPosition[3] = { 0,0,0 };
  this->Renderer->GetActiveCamera()->GetPosition(cameraPosition);
  pickPositions->GetPoint(0, pickPoint);
  double minDist2 = vtkMath::Distance2BetweenPoints(pickPoint, cameraPosition);
  for (vtkIdType i = 1; i < numberOfPickedPositions; i++)
    {
    double currentMinDist2 = vtkMath::Distance2BetweenPoints(pickPositions->GetPoint(i), cameraPosition);
    if (currentMinDist2<minDist2)
      {
      pickPositions->GetPoint(i, pickPoint);
      minDist2 = currentMinDist2;
      }
    }
  return true;
}

//----------------------------------------------------------------------
double vtkMRMLInteractionWidgetRepresentation3D::GetViewScaleFactorAtPosition(double positionWorld[3])
{
  double viewScaleFactorMmPerPixel = 1.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera())
    {
    return viewScaleFactorMmPerPixel;
    }

  vtkCamera * cam = this->Renderer->GetActiveCamera();
  if (cam->GetParallelProjection())
    {
    // Viewport: xmin, ymin, xmax, ymax; range: 0.0-1.0; origin is bottom left
    // Determine the available renderer size in pixels
    double minX = 0;
    double minY = 0;
    this->Renderer->NormalizedDisplayToDisplay(minX, minY);
    double maxX = 1;
    double maxY = 1;
    this->Renderer->NormalizedDisplayToDisplay(maxX, maxY);
    int rendererSizeInPixels[2] = { static_cast<int>(maxX - minX), static_cast<int>(maxY - minY) };
    // Parallel scale: height of the viewport in world-coordinate distances.
    // Larger numbers produce smaller images.
    viewScaleFactorMmPerPixel = (cam->GetParallelScale() * 2.0) / double(rendererSizeInPixels[1]);
    }
  else
    {
    double cameraFP[4] = { positionWorld[0], positionWorld[1], positionWorld[2], 1.0 };

    double cameraViewUp[3] = { 0 };
    cam->GetViewUp(cameraViewUp);
    vtkMath::Normalize(cameraViewUp);

    // Get distance in pixels between two points at unit distance above and below the focal point
    this->Renderer->SetWorldPoint(cameraFP[0] + cameraViewUp[0], cameraFP[1] + cameraViewUp[1], cameraFP[2] + cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double topCenter[3] = { 0 };
    this->Renderer->GetDisplayPoint(topCenter);
    topCenter[2] = 0.0;
    this->Renderer->SetWorldPoint(cameraFP[0] - cameraViewUp[0], cameraFP[1] - cameraViewUp[1], cameraFP[2] - cameraViewUp[2], cameraFP[3]);
    this->Renderer->WorldToDisplay();
    double bottomCenter[3] = { 0 };
    this->Renderer->GetDisplayPoint(bottomCenter);
    bottomCenter[2] = 0.0;
    double distInPixels = sqrt(vtkMath::Distance2BetweenPoints(topCenter, bottomCenter));

    // if render window is not initialized yet then distInPixels == 0.0,
    // in that case just leave the default viewScaleFactorMmPerPixel
    if (distInPixels > 1e-3)
      {
      // 2.0 = 2x length of viewUp vector in mm (because viewUp is unit vector)
      viewScaleFactorMmPerPixel = 2.0 / distInPixels;
      }
    }
  return viewScaleFactorMmPerPixel;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::UpdateViewScaleFactor()
{
  this->ViewScaleFactorMmPerPixel = 1.0;
  this->ScreenSizePixel = 1000.0;
  if (!this->Renderer || !this->Renderer->GetActiveCamera())
    {
    return;
    }

  int* screenSize = this->Renderer->GetRenderWindow()->GetScreenSize();
  double screenSizePixel = sqrt(screenSize[0] * screenSize[0] + screenSize[1] * screenSize[1]);
  if (screenSizePixel < 1.0)
    {
    // render window is not fully initialized yet
    return;
    }
  this->ScreenSizePixel = screenSizePixel;

  double cameraFP[3] = { 0.0 };
  this->Renderer->GetActiveCamera()->GetFocalPoint(cameraFP);
  this->ViewScaleFactorMmPerPixel = this->GetViewScaleFactorAtPosition(cameraFP);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::UpdateHandleSize()
{
  if (!this->DisplayNode->GetInteractionSizeAbsolute())
    {
    this->InteractionSize = this->ScreenSizePixel * this->ScreenScaleFactor
      * this->DisplayNode->GetInteractionScale() / 100.0 * this->ViewScaleFactorMmPerPixel;
    }
  else
    {
    this->InteractionSize = this->DisplayNode->GetInteractionSize();
    }
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidgetRepresentation3D::UpdateInteractionPipeline()
{
  vtkMRMLViewNode* viewNode = vtkMRMLViewNode::SafeDownCast(this->ViewNode);
  if (!viewNode)
    {
    this->Pipeline->Actor->SetVisibility(false);
    return;
    }
  // Final visibility handled by superclass in vtkMRMLInteractionWidgetRepresentation
  Superclass::UpdateInteractionPipeline();
}
