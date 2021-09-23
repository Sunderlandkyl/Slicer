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
#include "vtkMRMLMarkupsPlaneDisplayNode.h"
#include "vtkMRMLMarkupsPlaneNode.h"
#include "vtkMRMLSliceNode.h"
#include "vtkSlicerPlaneRepresentation2D.h"
#include "vtkSlicerPlaneRepresentation3D.h"

// VTK includes
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkMatrix4x4.h>
#include <vtkPointPlacer.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>

vtkStandardNewMacro(vtkSlicerPlaneWidget);

//----------------------------------------------------------------------
vtkSlicerPlaneWidget::vtkSlicerPlaneWidget()
{
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

  bool processedEvent = false;
  switch (widgetEvent)
  {
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
  return Superclass::ProcessMouseMove(eventData);
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

  MRMLNodeModifyBlocker blocker(markupsNode);

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
    if (index < 3 && rep3d)
      {
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, this->LastEventPosition, lastEventPos_World);
      this->GetClosestPointOnInteractionAxis(
        vtkMRMLMarkupsDisplayNode::ComponentScaleHandle, index, eventPos, eventPos_World);
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

    vtkNew<vtkMatrix4x4> objectToNodeMatrix;
    markupsNode->GetObjectToNodeMatrix(objectToNodeMatrix);

    vtkNew<vtkTransform> objectToNodeTransform;
    objectToNodeTransform->SetMatrix(objectToNodeMatrix);

    double newOrigin_Node[3] = { 0.0, 0.0, 0.0 };
    objectToNodeTransform->TransformPoint(newOrigin_Object, newOrigin_Node);
    markupsNode->SetCenter(newOrigin_Node);
    markupsNode->SetSize(newSize);

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
