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

// MRMLDM includes
#include "vtkMRMLInteractionWidget.h"
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLInteractionWidgetRepresentation.h"

// VTK includes
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkLine.h>
#include <vtkPlane.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>

// MRML includes
#include <vtkMRMLSliceNode.h>

//----------------------------------------------------------------------
vtkMRMLInteractionWidget::vtkMRMLInteractionWidget()
{
  this->LastEventPosition[0] = 0.0;
  this->LastEventPosition[1] = 0.0;
  this->StartEventOffsetPosition[0] = 0.0;
  this->StartEventOffsetPosition[1] = 0.0;

  // Update active component
  this->SetEventTranslation(WidgetStateIdle, vtkCommand::MouseMoveEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnWidget, vtkCommand::MouseMoveEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateIdle, vtkCommand::Move3DEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnWidget, vtkCommand::Move3DEvent, vtkEvent::NoModifier, WidgetEventMouseMove);

  // Handle interactions
  this->SetEventTranslationClickAndDrag(WidgetStateOnTranslationHandle, vtkCommand::LeftButtonPressEvent, vtkEvent::NoModifier,
    WidgetStateTranslate, WidgetEventTranslateStart, WidgetEventTranslateEnd);
  this->SetEventTranslation(WidgetStateOnTranslationHandle, vtkCommand::RightButtonPressEvent, vtkEvent::NoModifier, WidgetEventPick);
  this->SetEventTranslation(WidgetStateOnTranslationHandle, vtkMRMLInteractionEventData::RightButtonClickEvent, vtkEvent::NoModifier, WidgetEventMenu);

  this->SetEventTranslationClickAndDrag(WidgetStateOnRotationHandle, vtkCommand::LeftButtonPressEvent, vtkEvent::NoModifier,
    WidgetStateRotate, WidgetEventRotateStart, WidgetEventRotateEnd);
  this->SetEventTranslation(WidgetStateOnRotationHandle, vtkCommand::RightButtonPressEvent, vtkEvent::NoModifier, WidgetEventPick);
  this->SetEventTranslation(WidgetStateOnRotationHandle, vtkMRMLInteractionEventData::RightButtonClickEvent, vtkEvent::NoModifier, WidgetEventMenu);

  this->SetEventTranslationClickAndDrag(WidgetStateOnScaleHandle, vtkCommand::LeftButtonPressEvent, vtkEvent::NoModifier,
    WidgetStateScale, WidgetEventScaleStart, WidgetEventScaleEnd);
  this->SetEventTranslation(WidgetStateOnScaleHandle, vtkCommand::RightButtonPressEvent, vtkEvent::NoModifier, WidgetEventPick);
  this->SetEventTranslation(WidgetStateOnScaleHandle, vtkMRMLInteractionEventData::RightButtonClickEvent, vtkEvent::NoModifier, WidgetEventMenu);

  // Update active interaction handle component
  this->SetEventTranslation(WidgetStateOnTranslationHandle, vtkCommand::MouseMoveEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnTranslationHandle, vtkCommand::Move3DEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnRotationHandle, vtkCommand::MouseMoveEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnRotationHandle, vtkCommand::Move3DEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnScaleHandle, vtkCommand::MouseMoveEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
  this->SetEventTranslation(WidgetStateOnScaleHandle, vtkCommand::Move3DEvent, vtkEvent::NoModifier, WidgetEventMouseMove);
}

//----------------------------------------------------------------------
vtkMRMLInteractionWidget::~vtkMRMLInteractionWidget() = default;

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessWidgetRotateStart(vtkMRMLInteractionEventData* eventData)
{
  if ((this->WidgetState != vtkMRMLInteractionWidget::WidgetStateOnWidget && this->WidgetState != vtkMRMLInteractionWidget::WidgetStateOnRotationHandle))
    {
    return false;
    }

  this->SetWidgetState(WidgetStateRotate);
  this->StartWidgetInteraction(eventData);
  return true;
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessWidgetScaleStart(vtkMRMLInteractionEventData* eventData)
{
  if ((this->WidgetState != vtkMRMLInteractionWidget::WidgetStateOnWidget && this->WidgetState != vtkMRMLInteractionWidget::WidgetStateOnScaleHandle))
    {
    return false;
    }

  this->SetWidgetState(WidgetStateScale);
  this->StartWidgetInteraction(eventData);
  return true;
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessWidgetTranslateStart(vtkMRMLInteractionEventData* eventData)
{
  if ((this->WidgetState != vtkMRMLInteractionWidget::WidgetStateOnWidget && this->WidgetState != vtkMRMLInteractionWidget::WidgetStateOnTranslationHandle))
    {
    return false;
    }

  this->SetWidgetState(WidgetStateTranslate);
  this->StartWidgetInteraction(eventData);
  return true;
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessMouseMove(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!rep || !eventData)
    {
    return false;
    }

  int state = this->WidgetState;

  if (state == WidgetStateIdle
    || state == WidgetStateOnWidget
    || state == WidgetStateOnTranslationHandle
    || state == WidgetStateOnRotationHandle
    || state == WidgetStateOnScaleHandle)
    {
    // update state
    int foundComponentType = InteractionNone;
    int foundComponentIndex = -1;
    double closestDistance2 = 0.0;
    rep->CanInteract(eventData, foundComponentType, foundComponentIndex, closestDistance2);
    if (foundComponentType == InteractionNone)
      {
      this->SetWidgetState(WidgetStateIdle);
      }
    else if (foundComponentType == InteractionTranslationHandle)
      {
      this->SetWidgetState(WidgetStateOnTranslationHandle);
      }
    else if (foundComponentType == InteractionRotationHandle)
      {
      this->SetWidgetState(WidgetStateOnRotationHandle);
      }
    else if (foundComponentType == InteractionScaleHandle)
      {
      this->SetWidgetState(WidgetStateOnScaleHandle);
      }
    else
      {
      this->SetWidgetState(WidgetStateOnWidget);
      }

    this->SetActiveComponentIndex(foundComponentIndex);
    this->SetActiveComponentType(foundComponentType);
    }
  else
    {
    // Process the motion
    // Based on the displacement vector (computed in display coordinates) and
    // the cursor state (which corresponds to which part of the widget has been
    // selected), the widget points are modified.
    // First construct a local coordinate system based on the display coordinates
    // of the widget.
    double eventPos[2]
    {
      static_cast<double>(eventData->GetDisplayPosition()[0]),
      static_cast<double>(eventData->GetDisplayPosition()[1]),
    };

    if (state == WidgetStateTranslate)
      {
      this->TranslateWidget(eventPos);
      }
    else if (state == WidgetStateScale)
      {
      this->ScaleWidget(eventPos);
      }
    else if (state == WidgetStateRotate)
      {
      this->RotateWidget(eventPos);
      }

    this->LastEventPosition[0] = eventPos[0];
    this->LastEventPosition[1] = eventPos[1];
    }
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ConvertDisplayPositionToWorld(const int displayPos[2],
  double worldPos[3], double worldOrientationMatrix[9], double* refWorldPos/*=nullptr*/)
{
  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  double doubleDisplayPos[3] = { static_cast<double>(displayPos[0]), static_cast<double>(displayPos[1]), 0.0 };
  if (rep)
    {
    // 2D view
    rep->GetSliceToWorldCoordinates(doubleDisplayPos, worldPos);
    return true;
    }
  else
    {
    // 3D view
    bool preferPickOnSurface = true;
    if (refWorldPos != nullptr)
      {
      // TODO
      //// If reference position is provided then we may use that instead of picking on visible surface.
      //vtkMRMLDisplayNode* displayNode = this->GetDisplayNode();
      //if (displayNode)
      //  {
      //  preferPickOnSurface = (displayNode->GetSnapMode() == vtkMRMLDisplayNode::SnapModeToVisibleSurface);
      //  }
      }
    if (preferPickOnSurface)
      {
      // SnapModeToVisibleSurface
      // Try to pick on surface and pick on camera plane if nothing is found.
      //if (rep3d->AccuratePick(displayPos[0], displayPos[1], worldPos))
      //  {
      //  return true;
      //  }
      if (refWorldPos)
        {
        // Reference position is available (most likely, moving the point).
        return (rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
          doubleDisplayPos, refWorldPos, worldPos, worldOrientationMatrix));
        }
      }
    else
      {
      // SnapModeUnconstrained
      // Move the point relative to reference position, not restricted to surfaces if possible.
      if (refWorldPos)
        {
        // Reference position is available (most likely, moving the point).
        return (rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
          doubleDisplayPos, refWorldPos, worldPos, worldOrientationMatrix));
        }
      else
        {
        // Reference position is unavailable (e.g., not moving of an existing point but first placement)
        // Even if the constraining on the surface is no preferred, it is still better to
        // place it on a visible surface in 3D views rather on the .
        //if (rep3d->AccuratePick(displayPos[0], displayPos[1], worldPos))
        //  {
        //  return true;
        //  }
        }
      }
    // Last resort: place a point on the camera plane
    // (no reference position is available and no surface is visible there)
    return (rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      doubleDisplayPos, worldPos, worldOrientationMatrix));
    }
  return false;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidget::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &distance2)
{
  unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);
  if (widgetEvent == WidgetEventNone)
    {
    return false;
    }
  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!rep)
    {
    return false;
    }
  int eventid = eventData->GetType();

  // Currently interacting
  if (this->WidgetState == WidgetStateTranslate
    || this->WidgetState == WidgetStateRotate
    || this->WidgetState == WidgetStateScale)
    {
    distance2 = 0.0;
    return true;
    }

  int foundComponentType = InteractionNone;
  int foundComponentIndex = -1;
  double closestDistance2 = 0.0;
  rep->CanInteract(eventData, foundComponentType, foundComponentIndex, closestDistance2);
  if (foundComponentType == InteractionNone)
    {
    return false;
    }
  distance2 = closestDistance2;

  return true;
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessWidgetMenu(vtkMRMLInteractionEventData* eventData)
{
  if ((this->WidgetState != WidgetStateOnWidget &&
       this->WidgetState != WidgetStateOnTranslationHandle &&
       this->WidgetState != WidgetStateOnRotationHandle &&
       this->WidgetState != WidgetStateOnScaleHandle))
    {
    return false;
    }

  return true;
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessEndMouseDrag(vtkMRMLInteractionEventData* vtkNotUsed(eventData))
{
  if (!this->WidgetRep)
    {
    return false;
    }

  if ((this->WidgetState != vtkMRMLInteractionWidget::WidgetStateTranslate
    && this->WidgetState != vtkMRMLInteractionWidget::WidgetStateScale
    && this->WidgetState != vtkMRMLInteractionWidget::WidgetStateRotate
    ) || !this->WidgetRep)
    {
    return false;
    }

  int activeComponentType = this->GetActiveComponentType();
  if (activeComponentType == InteractionTranslationHandle)
    {
    this->SetWidgetState(WidgetStateOnTranslationHandle);
    }
  else if (activeComponentType == InteractionRotationHandle)
    {
    this->SetWidgetState(WidgetStateOnRotationHandle);
    }
  else if (activeComponentType == InteractionScaleHandle)
    {
    this->SetWidgetState(WidgetStateOnScaleHandle);
    }
  else
    {
    this->SetWidgetState(WidgetStateOnWidget);
    }

  this->EndWidgetInteraction();
  return true;
}

//-----------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);

  bool processedEvent = false;
  switch (widgetEvent)
    {
    case WidgetEventMouseMove:
      processedEvent = ProcessMouseMove(eventData);
      break;
    case WidgetEventMenu:
      processedEvent = ProcessWidgetMenu(eventData);
      break;
    case WidgetEventTranslateStart:
      processedEvent = ProcessWidgetTranslateStart(eventData);
      break;
    case WidgetEventTranslateEnd:
      processedEvent = ProcessEndMouseDrag(eventData);
      break;
    case WidgetEventRotateStart:
      processedEvent = ProcessWidgetRotateStart(eventData);
      break;
    case WidgetEventRotateEnd:
      processedEvent = ProcessEndMouseDrag(eventData);
      break;
    case WidgetEventScaleStart:
      processedEvent = ProcessWidgetScaleStart(eventData);
      break;
    case WidgetEventScaleEnd:
      processedEvent = ProcessEndMouseDrag(eventData);
      break;
    }

  return processedEvent;
}

//-----------------------------------------------------------------------------
void vtkMRMLInteractionWidget::Leave(vtkMRMLInteractionEventData* eventData)
{
  Superclass::Leave(eventData);
  this->SetActiveComponentType(InteractionNone);
  this->SetActiveComponentType(-1);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidget::StartWidgetInteraction(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!rep)
    {
    return;
    }

  double startEventPos[2]
    {
    static_cast<double>(eventData->GetDisplayPosition()[0]),
    static_cast<double>(eventData->GetDisplayPosition()[1])
    };

  // save the cursor position
  this->LastEventPosition[0] = startEventPos[0];
  this->LastEventPosition[1] = startEventPos[1];

  this->StartEventOffsetPosition[0] = 0;
  this->StartEventOffsetPosition[1] = 0;
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidget::EndWidgetInteraction()
{
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidget::TranslateWidget(double eventPos[2])
{
  double lastEventPos_World[3] = { 0.0 };
  double eventPos_World[3] = { 0.0 };
  double orientation_World[9] = { 0.0 };

  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  if (!rep)
    {
    return;
    }

  if (rep->GetSliceNode())
    {
    // 2D view
    double eventPos_Slice[3] = { 0. };
    eventPos_Slice[0] = this->LastEventPosition[0];
    eventPos_Slice[1] = this->LastEventPosition[1];
    rep->GetSliceToWorldCoordinates(eventPos_Slice, lastEventPos_World);

    eventPos_Slice[0] = eventPos[0];
    eventPos_Slice[1] = eventPos[1];
    rep->GetSliceToWorldCoordinates(eventPos_Slice, eventPos_World);
    }
  else
    {
    // 3D view
    double eventPos_Display[2] = { 0. };

    eventPos_Display[0] = this->LastEventPosition[0];
    eventPos_Display[1] = this->LastEventPosition[1];

    if (!rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    lastEventPos_World[0] = eventPos_World[0];
    lastEventPos_World[1] = eventPos_World[1];
    lastEventPos_World[2] = eventPos_World[2];

    eventPos_Display[0] = eventPos[0];
    eventPos_Display[1] = eventPos[1];

    if (!rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    }

  double translationVector_World[3];
  translationVector_World[0] = eventPos_World[0] - lastEventPos_World[0];
  translationVector_World[1] = eventPos_World[1] - lastEventPos_World[1];
  translationVector_World[2] = eventPos_World[2] - lastEventPos_World[2];

  int index = this->GetActiveComponentIndex();

  double translationAxis_World[3] = { 0 };
  rep->GetInteractionHandleAxisWorld(InteractionTranslationHandle, index, translationAxis_World);

  // Only perform constrained translation if the length of the axis is non-zero.
  if (vtkMath::Norm(translationAxis_World) > 0)
    {
    double lastEventPositionOnAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetClosestPointOnInteractionAxis(
      InteractionTranslationHandle, index, this->LastEventPosition, lastEventPositionOnAxis_World);

    double eventPositionOnAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetClosestPointOnInteractionAxis(
      InteractionTranslationHandle, index, eventPos, eventPositionOnAxis_World);

    vtkMath::Subtract(eventPositionOnAxis_World, lastEventPositionOnAxis_World, translationVector_World);
    double distance = vtkMath::Norm(translationVector_World);
    if (vtkMath::Dot(translationVector_World, translationAxis_World) < 0)
      {
      distance *= -1.0;
      }
    translationVector_World[0] = distance * translationAxis_World[0];
    translationVector_World[1] = distance * translationAxis_World[1];
    translationVector_World[2] = distance * translationAxis_World[2];
    }

  vtkNew<vtkTransform> translationTransform;
  translationTransform->Translate(translationVector_World);
  this->ApplyTransform(translationTransform);
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidget::ScaleWidget(double eventPos[2])
{
  double center[3] = { 0. };
  double ref[3] = { 0. };
  double worldPos[3], worldOrient[9];

  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  if (!rep)
    {
    return;
    }

  if (rep->GetSliceNode())
    {
    // 2D view
    double slicePos[3] = { 0. };
    slicePos[0] = this->LastEventPosition[0];
    slicePos[1] = this->LastEventPosition[1];
    rep->GetSliceToWorldCoordinates(slicePos, ref);

    slicePos[0] = eventPos[0];
    slicePos[1] = eventPos[1];
    rep->GetSliceToWorldCoordinates(slicePos, worldPos);

    rep->GetTransformationReferencePoint(center);
    }
  else
    {
    // 3D view
    double displayPos[2] = { 0. };
    displayPos[0] = this->LastEventPosition[0];
    displayPos[1] = this->LastEventPosition[1];
    if (rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      displayPos, ref, worldPos,
      worldOrient))
      {
      for (int i = 0; i < 3; i++)
        {
        ref[i] = worldPos[i];
        }
      }
    else
      {
      return;
      }
    displayPos[0] = eventPos[0];
    displayPos[1] = eventPos[1];

    if (!rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      displayPos, ref, worldPos,
      worldOrient))
      {
      return;
      }

    rep->GetTransformationReferencePoint(center);
    }

  double r2 = vtkMath::Distance2BetweenPoints(ref, center);
  double d2 = vtkMath::Distance2BetweenPoints(worldPos, center);
  if (d2 < 0.0000001)
    {
    return;
    }

  double ratio = sqrt(d2 / r2);

  // TODO
  /*this->ApplyTransform(scaleTransform);*/
}

//----------------------------------------------------------------------
void vtkMRMLInteractionWidget::RotateWidget(double eventPos[2])
{
  double eventPos_World[3] = { 0.0, 0.0, 0.0 };
  double lastEventPos_World[3] = { 0.0, 0.0, 0.0 };
  double orientation_World[9] = { 0.0, 0.0, 0.0 };
  double eventPos_Display[2] = { 0.0, 0.0 };

  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  if (!rep)
    {
    return;
    }

  if (rep->GetSliceNode())
    {
    // 2D view
    double eventPos_Slice[3] = { 0.0, 0.0, 0.0 };
    eventPos_Slice[0] = this->LastEventPosition[0];
    eventPos_Slice[1] = this->LastEventPosition[1];
    rep->GetSliceToWorldCoordinates(eventPos_Slice, lastEventPos_World);

    eventPos_Slice[0] = eventPos[0];
    eventPos_Slice[1] = eventPos[1];
    rep->GetSliceToWorldCoordinates(eventPos_Slice, eventPos_World);

    eventPos_Display[0] = eventPos_Slice[0];
    eventPos_Display[1] = eventPos_Slice[1];
    }
  else
    {
    // 3D view
    eventPos_Display[0] = this->LastEventPosition[0];
    eventPos_Display[1] = this->LastEventPosition[1];

    if (rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, eventPos_World, lastEventPos_World,
      orientation_World))
      {
      for (int i = 0; i < 3; i++)
        {
        eventPos_World[i] = lastEventPos_World[i];
        }
      }
    else
      {
      return;
      }
    eventPos_Display[0] = eventPos[0];
    eventPos_Display[1] = eventPos[1];

    if (!rep->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, eventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    }

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  rep->GetInteractionHandleOriginWorld(origin_World);

  double epsilon = 1e-5;
  double distance2 = vtkMath::Distance2BetweenPoints(eventPos_World, origin_World);
  if (distance2 < epsilon)
    {
    return;
    }

  for (int i = 0; i < 3; i++)
    {
    lastEventPos_World[i] -= origin_World[i];
    eventPos_World[i] -= origin_World[i];
    }

  double angle = vtkMath::DegreesFromRadians(
    vtkMath::AngleBetweenVectors(lastEventPos_World, eventPos_World));
  double rotationNormal_World[3] = { 0.0, 0.0, 0.0 };
  vtkMath::Cross(lastEventPos_World, eventPos_World, rotationNormal_World);
  double rotationAxis_World[3] = { 0.0, 1.0, 0.0 };
  int type = this->GetActiveComponentType();
  if (type == InteractionRotationHandle)
    {
    int index = this->GetActiveComponentIndex();
    double eventPositionOnAxisPlane_World[3] = { 0.0, 0.0, 0.0 };
    if (!this->GetIntersectionOnAxisPlane(type, index, eventPos, eventPositionOnAxisPlane_World))
      {
      vtkWarningMacro("RotateWidget: Could not calculate intended orientation");
      return;
      }

    rep->GetInteractionHandleAxisWorld(type, index, rotationAxis_World); // Axis of rotation
    double origin_World[3] = { 0.0, 0.0, 0.0 };
    rep->GetInteractionHandleOriginWorld(origin_World);

    double lastEventPositionOnAxisPlane_World[3] = { 0.0, 0.0, 0.0 };
    if (!this->GetIntersectionOnAxisPlane(
      InteractionRotationHandle, index, this->LastEventPosition,lastEventPositionOnAxisPlane_World))
      {
      vtkWarningMacro("RotateWidget: Could not calculate previous orientation");
      return;
      }

    double rotationHandleVector_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(lastEventPositionOnAxisPlane_World, origin_World, rotationHandleVector_World);

    double destinationVector_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(eventPositionOnAxisPlane_World, origin_World, destinationVector_World);

    angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(rotationHandleVector_World, destinationVector_World));
    vtkMath::Cross(rotationHandleVector_World, destinationVector_World, rotationNormal_World);
    }
  else
    {
    rotationAxis_World[0] = rotationNormal_World[0];
    rotationAxis_World[1] = rotationNormal_World[1];
    rotationAxis_World[2] = rotationNormal_World[2];
    }

  if (vtkMath::Dot(rotationNormal_World, rotationAxis_World) < 0.0)
    {
    angle *= -1.0;
    }

  vtkNew<vtkTransform> rotateTransform;
  rotateTransform->Translate(origin_World);
  rotateTransform->RotateWXYZ(angle, rotationAxis_World);
  rotateTransform->Translate(-origin_World[0], -origin_World[1], -origin_World[2]);
  this->ApplyTransform(rotateTransform);
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidget::GetIntersectionOnAxisPlane(int type, int index, const double input_Display[2], double outputIntersection_World[3])
{
  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  if (!rep)
    {
    return false;
    }

  double rotationAxis[3] = { 0 };
  rep->GetInteractionHandleAxisWorld(type, index, rotationAxis); // Axis of rotation
  double origin[3] = { 0, 0, 0 };
  rep->GetInteractionHandleOriginWorld(origin);

  vtkNew<vtkPlane> axisPlaneWorld;
  axisPlaneWorld->SetNormal(rotationAxis);
  axisPlaneWorld->SetOrigin(origin);

  double inputPoint0_World[3] = { 0.0, 0.0, 0.0 };
  double inputPoint1_World[3] = { 0.0, 0.0, 1.0 };
  double projectionVector_World[3] = { 0 };
  if (!rep->GetSliceNode())
    {
    // 3D view
    vtkRenderer* renderer = rep->GetRenderer();
    vtkCamera* camera = renderer->GetActiveCamera();

    // Focal point position
    double cameraFP_World[4] = { 0 };
    camera->GetFocalPoint(cameraFP_World);

    renderer->SetWorldPoint(cameraFP_World[0], cameraFP_World[1], cameraFP_World[2], cameraFP_World[3]);
    renderer->WorldToDisplay();
    double* cameraFP_Display = renderer->GetDisplayPoint();
    double selectionZ_Display = cameraFP_Display[2];

    renderer->SetDisplayPoint(input_Display[0], input_Display[1], selectionZ_Display);
    renderer->DisplayToWorld();
    double* input_World = renderer->GetWorldPoint();
    if (input_World[3] == 0.0)
      {
      vtkWarningMacro("Bad homogeneous coordinates");
      return false;
      }
    double pickPosition_World[3] = { 0.0 };
    for (int i = 0; i < 3; i++)
      {
      pickPosition_World[i] = input_World[i] / input_World[3];
      }
    if (camera->GetParallelProjection())
      {
      camera->GetDirectionOfProjection(projectionVector_World);
      for (int i = 0; i < 3; i++)
        {
        inputPoint0_World[i] = pickPosition_World[i];
        }
      }
    else
      {
      // Camera position
      double cameraPosition_World[4] = { 0.0 };
      camera->GetPosition(cameraPosition_World);

      //  Compute the ray endpoints.  The ray is along the line running from
      //  the camera position to the selection point, starting where this line
      //  intersects the front clipping plane, and terminating where this
      //  line intersects the back clipping plane.
      for (int i = 0; i < 3; i++)
        {
        projectionVector_World[i] = pickPosition_World[i] - cameraPosition_World[i];
        inputPoint0_World[i] = cameraPosition_World[i];
        }
      }
    vtkMath::Add(inputPoint0_World, projectionVector_World, inputPoint1_World);
    }
  else
    {
    // 2D view
    double inputPoint0_Display[3] = { input_Display[0], input_Display[1], 0.0 };
    double inputPoint1_Display[3] = { input_Display[0], input_Display[1], 1.0 };

    vtkNew<vtkTransform> displayToWorldTransform;
    vtkMRMLSliceNode* sliceNode = rep->GetSliceNode();
    vtkMatrix4x4* xyToRASMatrix = sliceNode->GetXYToRAS();
    displayToWorldTransform->SetMatrix(xyToRASMatrix);
    displayToWorldTransform->TransformPoint(inputPoint0_Display, inputPoint0_World);
    displayToWorldTransform->TransformPoint(inputPoint1_Display, inputPoint1_World);
    }

  double t = 0.0; // not used
  axisPlaneWorld->IntersectWithLine(inputPoint0_World, inputPoint1_World, t, outputIntersection_World);
  return true;
}

//----------------------------------------------------------------------
bool vtkMRMLInteractionWidget::GetClosestPointOnInteractionAxis(int type, int index, const double input_Display[2], double outputClosestPoint_World[3])
{
  vtkMRMLInteractionWidgetRepresentation* rep = vtkMRMLInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  if (!rep)
    {
    return false;
    }

  double translationAxis_World[3] = { 0 };
  rep->GetInteractionHandleAxisWorld(type, index, translationAxis_World); // Axis of rotation
  double origin_World[3] = { 0, 0, 0 };
  rep->GetInteractionHandleOriginWorld(origin_World);

  double inputPoint0_World[3] = { 0.0, 0.0, 0.0 };
  double inputPoint1_World[3] = { 0.0, 0.0, 1.0 };
  if (!rep->GetSliceNode())
    {
    // 3D view
    vtkRenderer* renderer = rep->GetRenderer();
    vtkCamera* camera = renderer->GetActiveCamera();

    // Focal point position
    double cameraFP_World[4] = { 0 };
    camera->GetFocalPoint(cameraFP_World);

    renderer->SetWorldPoint(cameraFP_World[0], cameraFP_World[1], cameraFP_World[2], cameraFP_World[3]);
    renderer->WorldToDisplay();
    double* displayCoords = renderer->GetDisplayPoint();
    double selectionZ = displayCoords[2];

    renderer->SetDisplayPoint(input_Display[0], input_Display[1], selectionZ);
    renderer->DisplayToWorld();
    double* input_World = renderer->GetWorldPoint();
    if (input_World[3] == 0.0)
      {
      vtkWarningMacro("Bad homogeneous coordinates");
      return false;
      }
    double pickPosition_World[3] = { 0 };
    for (int i = 0; i < 3; i++)
      {
      pickPosition_World[i] = input_World[i] / input_World[3];
      }

    double projectionVector_World[3] = { 0 };
    if (camera->GetParallelProjection())
      {
      camera->GetDirectionOfProjection(projectionVector_World);
      for (int i = 0; i < 3; i++)
        {
        inputPoint0_World[i] = pickPosition_World[i];
        }
      }
    else
      {
      // Camera position
      double cameraPosition_World[4] = { 0 };
      camera->GetPosition(cameraPosition_World);

      //  Compute the ray endpoints.  The ray is along the line running from
      //  the camera position to the selection point, starting where this line
      //  intersects the front clipping plane, and terminating where this
      //  line intersects the back clipping plane.
      for (int i = 0; i < 3; i++)
        {
        inputPoint0_World[i] = cameraPosition_World[i];
        projectionVector_World[i] = pickPosition_World[i] - cameraPosition_World[i];
        }
      }
    vtkMath::Add(inputPoint0_World, projectionVector_World, inputPoint1_World);
    }
  else
    {
    // 2D view
    double inputPoint0_Display[3] = { input_Display[0], input_Display[1], 0.0 };
    double inputPoint1_Display[3] = { input_Display[0], input_Display[1], 1.0 };

    vtkNew<vtkTransform> displayToWorldTransform;
    vtkMRMLSliceNode* sliceNode = rep->GetSliceNode();
    vtkMatrix4x4* xyToRASMatrix = sliceNode->GetXYToRAS();
    displayToWorldTransform->SetMatrix(xyToRASMatrix);
    displayToWorldTransform->TransformPoint(inputPoint0_Display, inputPoint0_World);
    displayToWorldTransform->TransformPoint(inputPoint1_Display, inputPoint1_World);
    }
  double t1; // not used
  double t2; // not used
  double closestPointNotUsed[3] = { 0 };
  double translationVectorPoint[3] = { 0 };
  vtkMath::Add(origin_World, translationAxis_World, translationVectorPoint);
  vtkLine::DistanceBetweenLines(origin_World, translationVectorPoint,
    inputPoint0_World, inputPoint1_World, outputClosestPoint_World, closestPointNotUsed, t1, t2);
  return true;
}

//-------------------------------------------------------------------------
bool vtkMRMLInteractionWidget::GetInteractive()
{
  switch (this->WidgetState)
    {
    case WidgetStateTranslate:
    case WidgetStateScale:
    case WidgetStateRotate:
      return true;
    default:
      return false;
    }
  return false;
}

//-------------------------------------------------------------------------
int vtkMRMLInteractionWidget::GetMouseCursor()
{
  if (this->WidgetState == WidgetStateIdle)
    {
    return VTK_CURSOR_DEFAULT;
    }
  else
    {
    return VTK_CURSOR_HAND;
    }
}
