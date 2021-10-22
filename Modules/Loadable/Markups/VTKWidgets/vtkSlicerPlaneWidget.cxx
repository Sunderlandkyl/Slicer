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
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

#include "vtkSlicerPlaneWidget.h"

#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLInteractionNode.h"
#include "vtkMRMLMarkupsPlaneDisplayNode.h"
#include "vtkMRMLMarkupsPlaneNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSliceNode.h"
#include "vtkSlicerApplicationLogic.h"
#include "vtkSlicerPlaneRepresentation2D.h"
#include "vtkSlicerPlaneRepresentation3D.h"

// VTK includes
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkMatrix4x4.h>
#include <vtkPlane.h>
#include <vtkPointPlacer.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>
#include <vtkWidgetEvent.h>

vtkStandardNewMacro(vtkSlicerPlaneWidget);

//----------------------------------------------------------------------
vtkSlicerPlaneWidget::vtkSlicerPlaneWidget()
{
  this->SetEventTranslation(WidgetStateDefine, vtkCommand::LeftButtonReleaseEvent, vtkEvent::AltModifier, WidgetEventControlPointPlacePlane);

  this->SetEventTranslationClickAndDrag(WidgetStateOnWidget, vtkCommand::LeftButtonPressEvent, vtkEvent::ShiftModifier,
    WidgetStateTranslatePlane, WidgetEventPlaneMoveStart, WidgetEventPlaneMoveEnd);
  this->SetEventTranslationClickAndDrag(WidgetStateOnScaleHandle, vtkCommand::LeftButtonPressEvent, vtkEvent::AltModifier,
    WidgetStateSymmetricScale, WidgetEventSymmetricScaleStart, WidgetEventSymmetricScaleEnd);
}

//----------------------------------------------------------------------
vtkSlicerPlaneWidget::~vtkSlicerPlaneWidget() = default;

//----------------------------------------------------------------------
void vtkSlicerPlaneWidget::CreateDefaultRepresentation(
  vtkMRMLMarkupsDisplayNode* markupsDisplayNode, vtkMRMLAbstractViewNode* viewNode, vtkRenderer* renderer)
{
  vtkSmartPointer<vtkSlicerMarkupsWidgetRepresentation> rep = nullptr;
  if (vtkMRMLSliceNode::SafeDownCast(viewNode))
    {
    rep = vtkSmartPointer<vtkSlicerPlaneRepresentation2D>::New();
    }
  else
    {
    rep = vtkSmartPointer<vtkSlicerPlaneRepresentation3D>::New();
    }
  this->SetRenderer(renderer);
  this->SetRepresentation(rep);
  rep->SetViewNode(viewNode);
  rep->SetMarkupsDisplayNode(markupsDisplayNode);
  rep->UpdateFromMRML(nullptr, 0); // full update
}

//-----------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double& distance2)
{
  vtkSlicerMarkupsWidgetRepresentation* rep = this->GetMarkupsRepresentation();
  if (!rep)
    {
    return false;
    }
  if (this->WidgetState == WidgetStateTranslatePlane)
    {
    distance2 = 0.0;
    return true;
    }
  else if (this->WidgetState == WidgetStateSymmetricScale)
    {
    distance2 = 0.0;
    return true;
    }
  else if (eventData->GetType() == vtkCommand::LeftButtonPressEvent &&
           !(eventData->GetModifiers() & vtkEvent::ShiftModifier))
    {
    // Because the plane widget is so large, we don't want to interrupt the mouse button down event if it is not necessary,
    // because we would interfere with the camera rotation function.
    // If the shift modifier is not enabled, then return false when the active component is not a plane.
    vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
    if (displayNode && displayNode->GetActiveComponentType() == vtkMRMLMarkupsDisplayNode::ComponentPlane)
      {
      return false;
      }
    }

  return Superclass::CanProcessInteractionEvent(eventData, distance2);
}

//-----------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);
  this->ApplicationLogic->PauseRender();

  bool processedEvent = false;
  switch (widgetEvent)
  {
  case WidgetEventControlPointPlacePlane:
    processedEvent = this->PlacePlane(eventData);
    break;
  case WidgetEventPlaneMoveStart:
    processedEvent = this->ProcessPlaneMoveStart(eventData);
    break;
  case WidgetEventMouseMove:
    processedEvent = this->ProcessMouseMove(eventData);
    break;
  case WidgetEventPlaneMoveEnd:
    processedEvent = this->ProcessPlaneMoveEnd(eventData);
    break;
  case WidgetEventSymmetricScaleStart:
    processedEvent = ProcessWidgetSymmetricScaleStart(eventData);
    break;
  case WidgetEventSymmetricScaleEnd:
    processedEvent = ProcessEndMouseDrag(eventData);
    break;
  }

  if (!processedEvent)
    {
    processedEvent = Superclass::ProcessInteractionEvent(eventData);
    }

  this->ApplicationLogic->ResumeRender();
  return processedEvent;
}

//----------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessPlaneMoveStart(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode || displayNode->GetActiveComponentType() != vtkMRMLMarkupsDisplayNode::ComponentPlane)
    {
    return false;
    }
  this->SetWidgetState(WidgetStateTranslatePlane);
  this->StartWidgetInteraction(eventData);
  return true;
}

//----------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessPlaneMoveEnd(vtkMRMLInteractionEventData* vtkNotUsed(eventData))
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode || displayNode->GetActiveComponentType() != vtkMRMLMarkupsDisplayNode::ComponentPlane)
    {
    return false;
    }
  this->SetWidgetState(WidgetStateOnWidget);
  this->EndWidgetInteraction();
  return true;
}

//-------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessUpdatePlaneFromViewNormal(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode)
    {
    return false;
    }

  // Get world position
  double eventPos_World[3] = { 0.0 };
  double eventOrientation_World[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
  if (eventData->IsWorldPositionValid() && eventData->IsWorldPositionAccurate())
    {
    eventData->GetWorldPosition(eventPos_World);

    double worldOrientationQuaternion[4] = { 0.0 };
    eventData->GetWorldOrientation(worldOrientationQuaternion);
    vtkMRMLMarkupsNode::ConvertOrientationWXYZToMatrix(worldOrientationQuaternion, eventOrientation_World);
    }
  else if (eventData->IsDisplayPositionValid())
    {
    int displayPos[2] = { 0 };
    eventData->GetDisplayPosition(displayPos);
    if (!this->ConvertDisplayPositionToWorld(displayPos, eventPos_World, eventOrientation_World))
      {
      eventData->GetWorldPosition(eventPos_World);
      }
    }
  eventData->SetWorldPosition(eventPos_World);

  vtkSlicerPlaneRepresentation2D* rep2d = vtkSlicerPlaneRepresentation2D::SafeDownCast(this->WidgetRep);
  vtkSlicerPlaneRepresentation3D* rep3d = vtkSlicerPlaneRepresentation3D::SafeDownCast(this->WidgetRep);

  double planeNormal_World[3] = { 0.0, 0.0, 0.0 };

  if (rep2d)
    {
    vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(rep2d->GetViewNode());
    if (sliceNode)
      {
      double normal4_World[4] = { 0.0, 0.0, -1.0, 0.0 };
      sliceNode->GetSliceToRAS()->MultiplyPoint(normal4_World, normal4_World);
      planeNormal_World[0] = normal4_World[0];
      planeNormal_World[1] = normal4_World[1];
      planeNormal_World[2] = normal4_World[2];
      }
    }
  else if (rep3d)
    {
    vtkCamera* camera = this->Renderer->GetActiveCamera();
    double cameraDirectionEventPos_World[3] = { 0.0, 0.0, 0.0 };
    double cameraDirectionLastEventPos_World[3] = { 0.0, 0.0, 0.0 };
    if (camera)
      {
      camera->GetDirectionOfProjection(planeNormal_World);
      // Face towards the camera
      vtkMath::MultiplyScalar(planeNormal_World, -1.0);
      }
    }
  vtkMath::Normalize(planeNormal_World);

  // Add/update control point position and orientation
  markupsNode->SetNormalWorld(planeNormal_World);

  markupsNode->SetIsPlaneValid(true);
  return true;
}

//-------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessWidgetSymmetricScaleStart(vtkMRMLInteractionEventData* eventData)
{
  if ((this->WidgetState != vtkSlicerMarkupsWidget::WidgetStateOnWidget && this->WidgetState != vtkSlicerMarkupsWidget::WidgetStateOnScaleHandle)
    || this->IsAnyControlPointLocked())
    {
    return false;
    }

  this->SetWidgetState(WidgetStateSymmetricScale);
  this->StartWidgetInteraction(eventData);
  return true;
}

//----------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessMouseMove(vtkMRMLInteractionEventData* eventData)
{
  if (this->WidgetState == WidgetStateTranslatePlane)
    {
    return this->ProcessPlaneTranslate(eventData);
    }
  else if (this->WidgetState == WidgetStateSymmetricScale)
    {
    return this->ProcessPlaneSymmetricScale(eventData);
    }

  bool processed = Superclass::ProcessMouseMove(eventData);

  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (planeNode && this->WidgetState == WidgetStateDefine && planeNode->GetPlaneType() == vtkMRMLMarkupsPlaneNode::PlaneTypePointNormal &&
      planeNode->GetNumberOfControlPoints() == 1 && this->PreviewPointIndex == 0)
    {
    // If we are using a point-normal plane type, then update the plane so that its normal is aligned with the view normal
    processed |= this->ProcessUpdatePlaneFromViewNormal(eventData);
    }

  return processed;
}

//----------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessPlaneTranslate(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode)
    {
    return false;
    }

  double eventPos[2]
    {
    static_cast<double>(eventData->GetDisplayPosition()[0]),
    static_cast<double>(eventData->GetDisplayPosition()[1]),
    };

  double ref[3] = { 0. };
  double worldPos[3], worldOrient[9];

  vtkSlicerPlaneRepresentation2D* rep2d = vtkSlicerPlaneRepresentation2D::SafeDownCast(this->WidgetRep);
  vtkSlicerPlaneRepresentation3D* rep3d = vtkSlicerPlaneRepresentation3D::SafeDownCast(this->WidgetRep);
  if (rep2d)
    {
    // 2D view
    double slicePos[3] = { 0. };
    slicePos[0] = this->LastEventPosition[0];
    slicePos[1] = this->LastEventPosition[1];
    rep2d->GetSliceToWorldCoordinates(slicePos, ref);

    slicePos[0] = eventPos[0];
    slicePos[1] = eventPos[1];
    rep2d->GetSliceToWorldCoordinates(slicePos, worldPos);
    }
  else if (rep3d)
    {
    // 3D view
    int displayPos[2] = { 0 };

    displayPos[0] = static_cast<int>(std::floor(this->LastEventPosition[0]));
    displayPos[1] = static_cast<int>(std::floor(this->LastEventPosition[1]));

    if (!this->ConvertDisplayPositionToWorld(displayPos, worldPos, worldOrient))
      {
      return false;
      }
    ref[0] = worldPos[0];
    ref[1] = worldPos[1];
    ref[2] = worldPos[2];

    displayPos[0] = eventPos[0];
    displayPos[1] = eventPos[1];

    if (!this->ConvertDisplayPositionToWorld(displayPos, worldPos, worldOrient))
      {
      return false;
      }
    }

  double vector_World[3];
  vector_World[0] = worldPos[0] - ref[0];
  vector_World[1] = worldPos[1] - ref[1];
  vector_World[2] = worldPos[2] - ref[2];

  bool lockToNormal = false;
  if (lockToNormal)
    {
    double normal[3] = { 0 };
    markupsNode->GetNormal(normal);

    double magnitude = vtkMath::Dot(vector_World, normal);
    vtkMath::MultiplyScalar(normal, magnitude);
    for (int i = 0; i < 3; ++i)
      {
      vector_World[i] = normal[i];
      }
    }

  MRMLNodeModifyBlocker blocker(markupsNode);

  vtkNew<vtkMatrix4x4> worldToObjectMatrix;
  markupsNode->GetObjectToWorldMatrix(worldToObjectMatrix);
  worldToObjectMatrix->Invert();

  vtkNew<vtkTransform> worldToObjectTransform;
  worldToObjectTransform->PostMultiply();
  worldToObjectTransform->SetMatrix(worldToObjectMatrix);
  worldToObjectTransform->Concatenate(markupsNode->GetObjectToBaseMatrix());

  double vector_Plane[3] = { 0.0 };
  worldToObjectTransform->TransformVector(vector_World, vector_Plane);

  vtkNew<vtkTransform> objectToBaseTransform;
  objectToBaseTransform->PostMultiply();
  objectToBaseTransform->SetMatrix(markupsNode->GetObjectToBaseMatrix());
  objectToBaseTransform->Translate(vector_Plane);
  markupsNode->GetObjectToBaseMatrix()->DeepCopy(objectToBaseTransform->GetMatrix());

  markupsNode->InvokeCustomModifiedEvent(vtkMRMLMarkupsNode::PointModifiedEvent);

  this->LastEventPosition[0] = eventPos[0];
  this->LastEventPosition[1] = eventPos[1];
  return true;
}

//----------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessPlaneSymmetricScale(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsNode* markupsNode = this->GetMarkupsNode();
  vtkSlicerMarkupsWidgetRepresentation* rep = this->GetMarkupsRepresentation();
  if (!rep || !markupsNode || !eventData)
    {
    return false;
    }

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

  this->ScaleWidget(eventPos, true);

  this->LastEventPosition[0] = eventPos[0];
  this->LastEventPosition[1] = eventPos[1];

  return true;
}

//-------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::ProcessEndMouseDrag(vtkMRMLInteractionEventData* eventData)
{
  if (!this->WidgetRep)
    {
    return false;
    }

  if (this->WidgetState == vtkSlicerPlaneWidget::WidgetStateSymmetricScale)
    {
    int activeComponentType = this->GetActiveComponentType();
    if (activeComponentType == vtkMRMLMarkupsDisplayNode::ComponentScaleHandle)
      {
      this->SetWidgetState(WidgetStateOnScaleHandle);
      }
    else
      {
      this->SetWidgetState(WidgetStateOnWidget);
      }
    this->EndWidgetInteraction();
    }

  return Superclass::ProcessEndMouseDrag(eventData);
}

//----------------------------------------------------------------------
void vtkSlicerPlaneWidget::RotateWidget(double eventPos[2])
{
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode)
    {
    return;
    }

  double eventPos_World[3] = { 0. };
  double lastEventPos_World[3] = { 0. };
  double orientation_World[9] = { 0. };
  double eventPos_Display[2] = { 0. };

  vtkSlicerMarkupsWidgetRepresentation* rep = vtkSlicerMarkupsWidgetRepresentation::SafeDownCast(this->WidgetRep);
  vtkSlicerMarkupsWidgetRepresentation2D* rep2d = vtkSlicerMarkupsWidgetRepresentation2D::SafeDownCast(this->WidgetRep);
  vtkSlicerMarkupsWidgetRepresentation3D* rep3d = vtkSlicerMarkupsWidgetRepresentation3D::SafeDownCast(this->WidgetRep);
  if (rep2d)
    {
    double eventPos_Slice[3] = { 0. };
    eventPos_Slice[0] = this->LastEventPosition[0];
    eventPos_Slice[1] = this->LastEventPosition[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, lastEventPos_World);

    eventPos_Slice[0] = eventPos[0];
    eventPos_Slice[1] = eventPos[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, eventPos_World);

    eventPos_Display[0] = eventPos_Slice[0];
    eventPos_Display[1] = eventPos_Slice[1];
    }
  else if (rep3d)
    {
    eventPos_Display[0] = this->LastEventPosition[0];
    eventPos_Display[1] = this->LastEventPosition[1];

    if (rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
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

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, eventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    }

  double origin_World[3] = { 0.0 };
  rep->GetInteractionHandleOriginWorld(origin_World);

  double epsilon = 1e-5;
  double d2 = vtkMath::Distance2BetweenPoints(eventPos_World, origin_World);
  if (d2 < epsilon)
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
  double rotationNormal_World[3] = { 0.0 };
  vtkMath::Cross(lastEventPos_World, eventPos_World, rotationNormal_World);
  double rotationAxis_World[3] = { 0.0, 1.0, 0.0 };
  int type = this->GetActiveComponentType();
  if (type == vtkMRMLMarkupsDisplayNode::ComponentRotationHandle)
    {
    int index = this->GetMarkupsDisplayNode()->GetActiveComponentIndex();
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
      vtkMRMLMarkupsDisplayNode::ComponentRotationHandle, index, this->LastEventPosition,lastEventPositionOnAxisPlane_World))
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

  vtkNew<vtkMatrix4x4> objectToNodeMatrix;
  markupsNode->GetObjectToNodeMatrix(objectToNodeMatrix);

  vtkNew<vtkMatrix4x4> nodeToObjectMatrix;
  vtkMatrix4x4::Invert(objectToNodeMatrix, nodeToObjectMatrix);

  vtkNew<vtkTransform> objectToNodeTransform;
  objectToNodeTransform->SetMatrix(objectToNodeMatrix);

  vtkNew<vtkMatrix4x4> worldToObjectMatrix;
  markupsNode->GetObjectToWorldMatrix(worldToObjectMatrix);
  worldToObjectMatrix->Invert();
  vtkNew<vtkTransform> worldToObjectTransform;
  worldToObjectTransform->SetMatrix(worldToObjectMatrix);

  double rotationAxis_Object[3] = { 0.0, 0.0, 0.0 };
  worldToObjectTransform->TransformVector(rotationAxis_World, rotationAxis_Object);

  vtkNew<vtkTransform> rotateTransform;
  rotateTransform->PostMultiply();
  rotateTransform->Concatenate(nodeToObjectMatrix);
  rotateTransform->RotateWXYZ(angle, rotationAxis_Object);
  rotateTransform->Concatenate(objectToNodeMatrix);
  markupsNode->ApplyTransform(rotateTransform);
}


//----------------------------------------------------------------------
void vtkSlicerPlaneWidget::ScaleWidget(double eventPos[2])
{
  this->ScaleWidget(eventPos, false);
}

//----------------------------------------------------------------------
void vtkSlicerPlaneWidget::ScaleWidget(double eventPos[2], bool symmetricScale)
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode || !displayNode)
    {
    return;
    }

  double lastEventPos_World[3] = { 0.0 };
  double eventPos_World[3] = { 0.0 };
  double orientation_World[9] = { 0.0 };

  vtkSlicerPlaneRepresentation2D* rep2d = vtkSlicerPlaneRepresentation2D::SafeDownCast(this->WidgetRep);
  vtkSlicerPlaneRepresentation3D* rep3d = vtkSlicerPlaneRepresentation3D::SafeDownCast(this->WidgetRep);
  if (rep2d)
    {
    // 2D view
    double eventPos_Slice[3] = { 0. };
    eventPos_Slice[0] = this->LastEventPosition[0];
    eventPos_Slice[1] = this->LastEventPosition[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, lastEventPos_World);

    eventPos_Slice[0] = eventPos[0];
    eventPos_Slice[1] = eventPos[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, eventPos_World);
    }
  else if (rep3d)
    {
    // 3D view
    double eventPos_Display[2] = { 0. };
    eventPos_Display[0] = this->LastEventPosition[0];
    eventPos_Display[1] = this->LastEventPosition[1];

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
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

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    }

  if (this->GetActiveComponentType() == vtkMRMLMarkupsDisplayNode::ComponentScaleHandle)
    {
    vtkNew<vtkMatrix4x4> worldToObjectMatrix;
    markupsNode->GetObjectToWorldMatrix(worldToObjectMatrix);
    worldToObjectMatrix->Invert();
    vtkNew<vtkTransform> worldToObjectTransform;
    worldToObjectTransform->SetMatrix(worldToObjectMatrix);

    int index = displayNode->GetActiveComponentIndex();
    if (index <= vtkMRMLMarkupsPlaneDisplayNode::HandleAEdge)
      {
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, this->LastEventPosition, lastEventPos_World);
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, eventPos, eventPos_World);
      }
    else
      {
      double normal_World[3] = { 0.0, 0.0, 0.0 };
      markupsNode->GetNormalWorld(normal_World);

      double origin_World[3] = { 0.0, 0.0, 0.0 };
      markupsNode->GetOriginWorld(origin_World);

      vtkNew<vtkPlane> plane;
      plane->SetOrigin(origin_World);
      plane->SetNormal(normal_World);

      double cameraDirectionEventPos_World[3] = { 0.0, 0.0, 0.0 };
      double cameraDirectionLastEventPos_World[3] = { 0.0, 0.0, 0.0 };
      if (rep2d)
        {
        vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(rep2d->GetViewNode());
        if (sliceNode)
          {
          double normal4_World[4] = { 0.0, 0.0, 1.0, 0.0 };
          sliceNode->GetSliceToRAS()->MultiplyPoint(normal4_World, normal4_World);
          cameraDirectionEventPos_World[0] = normal4_World[0];
          cameraDirectionEventPos_World[1] = normal4_World[1];
          cameraDirectionEventPos_World[2] = normal4_World[2];
          cameraDirectionLastEventPos_World[0] = normal4_World[0];
          cameraDirectionLastEventPos_World[1] = normal4_World[1];
          cameraDirectionLastEventPos_World[2] = normal4_World[2];
          }
        }
      else if (rep3d)
        {
        vtkCamera* camera = this->Renderer->GetActiveCamera();
        if (camera && camera->GetParallelProjection())
          {
          camera->GetDirectionOfProjection(cameraDirectionEventPos_World);
          camera->GetDirectionOfProjection(cameraDirectionLastEventPos_World);
          }
        else if (camera)
          {
          // Camera position
          double cameraPosition_World[4] = { 0.0 };
          camera->GetPosition(cameraPosition_World);

          //  Compute the ray endpoints.  The ray is along the line running from
          //  the camera position to the selection point, starting where this line
          //  intersects the front clipping plane, and terminating where this
          //  line intersects the back clipping plane.
          vtkMath::Subtract(cameraPosition_World, eventPos_World, cameraDirectionEventPos_World);
          vtkMath::Subtract(cameraPosition_World, lastEventPos_World, cameraDirectionLastEventPos_World);
          }
        }

      double eventPos2_World[3];
      vtkMath::Add(eventPos_World, cameraDirectionEventPos_World, eventPos2_World);

      double lastEventPos2_World[3];
      vtkMath::Add(lastEventPos_World, cameraDirectionLastEventPos_World, lastEventPos2_World);
      double t;
      plane->IntersectWithLine(eventPos_World, eventPos2_World, t, eventPos_World);
      plane->IntersectWithLine(lastEventPos_World, lastEventPos2_World, t, lastEventPos_World);
      }

    double scaleVector_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(eventPos_World, lastEventPos_World, scaleVector_World);

    double scaleVector_Plane[3] = { 0.0, 0.0, 0.0 };
    worldToObjectTransform->TransformVector(scaleVector_World, scaleVector_Plane);

    double oldSize_Plane[2] = { 0.0, 0.0 };
    markupsNode->GetSize(oldSize_Plane);

    double bounds_Plane[4] = { -oldSize_Plane[0]/2.0, oldSize_Plane[0]/2.0, -oldSize_Plane[1]/2.0, oldSize_Plane[1]/2.0 };

    switch (index)
      {
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLEdge:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner:
        bounds_Plane[0] += scaleVector_Plane[0];
        if (symmetricScale)
          {
          bounds_Plane[1] -= scaleVector_Plane[0];
          }
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleREdge:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner:
        bounds_Plane[1] += scaleVector_Plane[0];
        if (symmetricScale)
          {
          bounds_Plane[0] -= scaleVector_Plane[0];
          }
        break;
      default:
        break;
      }

    switch (index)
      {
      case vtkMRMLMarkupsPlaneDisplayNode::HandleAEdge:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner:
        bounds_Plane[2] += scaleVector_Plane[1];
        if (symmetricScale)
          {
          bounds_Plane[3] -= scaleVector_Plane[1];
          }
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandlePEdge:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner:
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner:
        bounds_Plane[3] += scaleVector_Plane[1];
        if (symmetricScale)
          {
          bounds_Plane[2] -= scaleVector_Plane[1];
          }
        break;
      default:
        break;
      }

    double newSize[2] = { 0.0, 0.0 };
    double newOrigin_Object[3] = { 0.0, 0.0, 0.0 };
    for (int i = 0; i < 2; ++i)
      {
      newSize[i] = std::abs(bounds_Plane[2 * i + 1] - bounds_Plane[2 * i]);
      newOrigin_Object[i] = (bounds_Plane[2 * i + 1] + bounds_Plane[2 * i]) / 2.0;
      }

    vtkNew<vtkMatrix4x4> objectToWorldMatrix;
    markupsNode->GetObjectToWorldMatrix(objectToWorldMatrix);
    vtkNew<vtkTransform> objectToWorldTransform;
    objectToWorldTransform->SetMatrix(objectToWorldMatrix);

    double newOrigin_World[3] = { 0.0, 0.0, 0.0 };
    objectToWorldTransform->TransformPoint(newOrigin_Object, newOrigin_World);

    MRMLNodeModifyBlocker blocker(markupsNode);
    markupsNode->SetSize(newSize);
    markupsNode->SetOriginWorld(newOrigin_World);

    bool flipLRHandle = bounds_Plane[1] < bounds_Plane[0];
    bool flipPAHandle = bounds_Plane[3] < bounds_Plane[2];
    if (flipLRHandle || flipPAHandle)
      {
      this->FlipPlaneHandles(flipLRHandle, flipPAHandle);
      }
    }
}

//----------------------------------------------------------------------
void vtkSlicerPlaneWidget::FlipPlaneHandles(bool flipLRHandle, bool flipPAHandle)
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode || !displayNode)
    {
    return;
    }

  int index = displayNode->GetActiveComponentIndex();
  if (flipLRHandle)
    {
    switch (index)
      {
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLEdge:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleREdge;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleREdge:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleLEdge;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner;
        break;
      default:
        break;
      }
    }

  if (flipPAHandle)
    {
    switch (index)
      {
      case vtkMRMLMarkupsPlaneDisplayNode::HandleAEdge:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandlePEdge;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandlePEdge:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleAEdge;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleLPCorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleLACorner;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner;
        break;
      case vtkMRMLMarkupsPlaneDisplayNode::HandleRPCorner:
        index = vtkMRMLMarkupsPlaneDisplayNode::HandleRACorner;
        break;
      default:
        break;
      }
    }

  displayNode->SetActiveComponent(displayNode->GetActiveComponentType(), index);
}

//---------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::PlacePoint(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsPlaneNode* markupsNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode)
    {
      return false;
    }

  bool success = Superclass::PlacePoint(eventData);
  if (!success)
    {
    return false;
    }

  // We have returned to place mode. Need to delete the uneccesary points
  vtkMRMLInteractionNode* interactionNode = this->GetInteractionNode();
  if (interactionNode && interactionNode->GetCurrentInteractionMode() == vtkMRMLInteractionNode::ViewTransform)
    {
    if (markupsNode->GetPlaneType() == vtkMRMLMarkupsPlaneNode::PlaneTypePointNormal)
      {
      markupsNode->UpdateControlPointsFromPlane();
      }
    }
  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerPlaneWidget::PlacePlane(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsPlaneNode* planeNode = vtkMRMLMarkupsPlaneNode::SafeDownCast(this->GetMarkupsNode());
  if (!planeNode)
    {
    return false;
    }

  bool handled = this->PlacePoint(eventData);
  planeNode->UpdateControlPointsFromPlane();

  int controlPointIndex = this->PreviewPointIndex;
  // Convert the preview point to a proper control point
  this->PreviewPointIndex = -1;

  // if this was a one time place, go back to view transform mode
  vtkMRMLInteractionNode *interactionNode = this->GetInteractionNode();

  if(interactionNode && !interactionNode->GetPlaceModePersistence())
    {
      vtkDebugMacro("End of one time place, place mode persistence = " << interactionNode->GetPlaceModePersistence());
      interactionNode->SetCurrentInteractionMode(vtkMRMLInteractionNode::ViewTransform);

      // The mouse is over the control point and we are not in place mode anymore
      if (this->GetMarkupsDisplayNode())
        {
        this->GetMarkupsDisplayNode()->SetActiveControlPoint(controlPointIndex);
        }
      this->WidgetState = WidgetStateOnWidget;
    }

  //bool success = (controlPointIndex >= 0);

  return handled;
}
