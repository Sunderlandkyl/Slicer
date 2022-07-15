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

#include "vtkMRMLFocusWidget.h"

// VTK includes
#include "vtkCamera.h"
#include "vtkEvent.h"
#include "vtkInteractorStyle.h"
#include "vtkPlane.h"
#include "vtkRenderer.h"
#include "vtkRenderWindow.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkTransform.h"

/// MRML includes
#include "vtkMRMLCrosshairDisplayableManager.h"
#include "vtkMRMLCrosshairNode.h"
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLInteractionNode.h"
#include "vtkMRMLLayoutNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLViewNode.h"

vtkStandardNewMacro(vtkMRMLFocusWidget);

//----------------------------------------------------------------------------------
vtkMRMLFocusWidget::vtkMRMLFocusWidget()
{
  this->SetKeyboardEventTranslation(WidgetStateFocus, vtkEvent::NoModifier, 0, 0, "Escape", WidgetEventCancelFocus);
}

//----------------------------------------------------------------------------------
vtkMRMLFocusWidget::~vtkMRMLFocusWidget()
{
}

//----------------------------------------------------------------------
void vtkMRMLFocusWidget::CreateDefaultRepresentation()
{
  // This widget has no visible representation
  return;
}

//-----------------------------------------------------------------------------
bool vtkMRMLFocusWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &distance2)
{
  //if (eventData->GetType() == vtkCommand::LeaveEvent)
  //  {
  //  // We cannot capture keypress events until the user clicks in the view
  //  // so when we are outside then we should assume that modifier
  //  // is not just "stuck".
  //  this->ModifierKeyPressedSinceLastClickAndDrag = true;
  //  }
  //if (eventData->GetType() == vtkCommand::KeyPressEvent)
  //  {
  //  if (eventData->GetKeySym().find("Shift") != std::string::npos)
  //    {
  //    this->ModifierKeyPressedSinceLastClickAndDrag = true;
  //    }
  //  }

  //unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);
  //if (widgetEvent == WidgetEventNone)
  //  {
  //  // If this event is not recognized then give a chance to process it as a click event.
  //  return this->CanProcessButtonClickEvent(eventData, distance2);
  //  }

  //// If we are currently dragging a point then we interact everywhere
  //if (this->WidgetState == WidgetStateTranslate
  //  || this->WidgetState == WidgetStateRotate
  //  || this->WidgetState == WidgetStateScale
  //  || this->WidgetState == WidgetStateMoveCrosshair
  //  || this->WidgetState == WidgetStateSpin)
  //  {
  //  distance2 = 0.0;
  //  return true;
  //  }

  //// By processing the SetCrosshairPosition action at this point, rather than in ProcessInteractionEvent,
  //// we allow other widgets to perform actions at the same time.
  //// For example, this allows markup preview to remain visible in place mode while adjusting slice position
  //// with shift + mouse-move.
  //if (widgetEvent == WidgetEventSetCrosshairPositionBackground)
  //  {
  //  this->ProcessSetCrosshairBackground(eventData);
  //  }

  //distance2 = 1e10; // we can process this event but we let more specific widgets to claim it (if they are closer)
  //return true;
  return false;
}

//-----------------------------------------------------------------------------
bool vtkMRMLFocusWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  //if (!this->CameraNode)
  //  {
  //  return false;
  //  }
  //unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);

  //bool processedEvent = true;

  //switch (widgetEvent)
  //  {
  //  case WidgetEventCameraRotateToAnterior:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateTo(vtkMRMLCameraNode::Anterior);
  //    break;
  //  case WidgetEventCameraRotateToPosterior:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateTo(vtkMRMLCameraNode::Posterior);
  //    break;
  //  case WidgetEventCameraRotateToRight:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateTo(vtkMRMLCameraNode::Right);
  //    break;
  //  case WidgetEventCameraRotateToLeft:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateTo(vtkMRMLCameraNode::Left);
  //    break;
  //  case WidgetEventCameraRotateToSuperior:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateTo(vtkMRMLCameraNode::Superior);
  //    break;
  //  case WidgetEventCameraRotateToInferior:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateTo(vtkMRMLCameraNode::Inferior);
  //    break;

  //  case WidgetEventCameraTranslateBackwardX:
  //    this->SaveStateForUndo();
  //    this->CameraNode->TranslateAlong(vtkMRMLCameraNode::X, true); // screen X is towards left
  //    break;
  //  case WidgetEventCameraTranslateForwardX:
  //    this->SaveStateForUndo();
  //    this->CameraNode->TranslateAlong(vtkMRMLCameraNode::X, false); // screen X is towards left
  //    break;
  //  case WidgetEventCameraTranslateBackwardY:
  //    this->SaveStateForUndo();
  //    this->CameraNode->TranslateAlong(vtkMRMLCameraNode::Y, false);
  //    break;
  //  case WidgetEventCameraTranslateForwardY:
  //    this->SaveStateForUndo();
  //    this->CameraNode->TranslateAlong(vtkMRMLCameraNode::Y, true);
  //    break;
  //  case WidgetEventCameraTranslateBackwardZ:
  //    this->SaveStateForUndo();
  //    this->CameraNode->TranslateAlong(vtkMRMLCameraNode::Z, false);
  //    break;
  //  case WidgetEventCameraTranslateForwardZ:
  //    this->SaveStateForUndo();
  //    this->CameraNode->TranslateAlong(vtkMRMLCameraNode::Z, true);
  //    break;

  //  case WidgetEventCameraRotateCcwX:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateAround(vtkMRMLCameraNode::R, false);
  //    break;
  //  case WidgetEventCameraRotateCwX:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateAround(vtkMRMLCameraNode::R, true);
  //    break;
  //  case WidgetEventCameraRotateCcwY:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateAround(vtkMRMLCameraNode::A, false);
  //    break;
  //  case WidgetEventCameraRotateCwY:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateAround(vtkMRMLCameraNode::A, true);
  //    break;
  //  case WidgetEventCameraRotateCcwZ:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateAround(vtkMRMLCameraNode::S, false);
  //    break;
  //  case WidgetEventCameraRotateCwZ:
  //    this->SaveStateForUndo();
  //    this->CameraNode->RotateAround(vtkMRMLCameraNode::S, true);
  //    break;

  //  case WidgetEventToggleCameraTiltLock:
  //      this->CameraTiltLocked = !this->CameraTiltLocked;
  //      break;

  //  case WidgetEventCameraReset:
  //    this->SaveStateForUndo();
  //    this->CameraNode->Reset(true, true, true, this->Renderer);
  //    break;
  //  case WidgetEventCameraResetRotation:
  //    this->SaveStateForUndo();
  //    this->CameraNode->Reset(true, false, false, this->Renderer);
  //    break;
  //  case WidgetEventCameraResetTranslation:
  //    this->SaveStateForUndo();
  //    this->CameraNode->Reset(false, true, false, this->Renderer);
  //    break;
  //  case WidgetEventCameraResetFieldOfView:
  //    this->SaveStateForUndo();
  //    if (this->Renderer != nullptr)
  //      {
  //      this->Renderer->ResetCamera();
  //      }
  //    break;
  //  case WidgetEventCameraZoomIn:
  //    this->SaveStateForUndo();
  //    this->Dolly(1.2);
  //    break;
  //  case WidgetEventCameraZoomOut:
  //    this->SaveStateForUndo();
  //    this->Dolly(0.8);
  //    break;

  //  case WidgetEventCameraWheelZoomIn:
  //    this->SaveStateForUndo();
  //    this->Dolly(pow((double)1.1, 0.2 * this->MotionFactor * this->MouseWheelMotionFactor));
  //    break;
  //  case WidgetEventCameraWheelZoomOut:
  //    this->SaveStateForUndo();
  //    this->Dolly(pow((double)1.1, -0.2 * this->MotionFactor * this->MouseWheelMotionFactor));
  //    break;

  //  case WidgetEventMouseMove:
  //    // click-and-dragging the mouse cursor
  //    processedEvent = this->ProcessMouseMove(eventData);
  //    break;

  //  case WidgetEventTranslateStart:
  //    this->SetWidgetState(WidgetStateTranslate);
  //    processedEvent = this->ProcessStartMouseDrag(eventData);
  //    break;
  //  case WidgetEventTranslateEnd:
  //    processedEvent = this->ProcessEndMouseDrag(eventData);
  //    break;
  //  case WidgetEventRotateStart:
  //    this->SetWidgetState(WidgetStateRotate);
  //    processedEvent = this->ProcessStartMouseDrag(eventData);
  //    break;
  //  case WidgetEventRotateEnd:
  //    processedEvent = this->ProcessEndMouseDrag(eventData);
  //    break;
  //  case WidgetEventScaleStart:
  //    this->SetWidgetState(WidgetStateScale);
  //    processedEvent = this->ProcessStartMouseDrag(eventData);
  //    break;
  //  case WidgetEventScaleEnd:
  //    processedEvent = this->ProcessEndMouseDrag(eventData);
  //    break;
  //  case WidgetEventMoveCrosshairStart:
  //    this->SetWidgetState(WidgetStateMoveCrosshair);
  //    processedEvent = this->ProcessStartMouseDrag(eventData);
  //    break;
  //  case WidgetEventMoveCrosshairEnd:
  //    processedEvent = this->ProcessEndMouseDrag(eventData);
  //    break;
  //  case WidgetEventSpinStart:
  //    this->SetWidgetState(WidgetStateSpin);
  //    processedEvent = this->ProcessStartMouseDrag(eventData);
  //    break;
  //  case WidgetEventSpinEnd:
  //    processedEvent = this->ProcessEndMouseDrag(eventData);
  //    break;
  //  case WidgetEventTouchGestureStart:
  //    this->ProcessTouchGestureStart(eventData);
  //    break;
  //  case WidgetEventTouchGestureEnd:
  //    this->ProcessTouchGestureEnd(eventData);
  //    break;
  //  case WidgetEventTouchSpinCamera:
  //    this->ProcessTouchCameraSpin(eventData);
  //    break;
  //  case WidgetEventTouchPinchZoom:
  //    this->ProcessTouchCameraZoom(eventData);
  //    break;
  //  case WidgetEventTouchPanTranslate:
  //    this->ProcessTouchCameraTranslate(eventData);
  //    break;
  //  case WidgetEventMenu:
  //    processedEvent = this->ProcessWidgetMenu(eventData);
  //    break;

  //  case WidgetEventSetCrosshairPosition:
  //    this->ProcessSetCrosshair(eventData);
  //    break;

  //  case WidgetEventMaximizeView:
  //    processedEvent = this->ProcessMaximizeView(eventData);
  //    break;

  //  default:
  //    processedEvent = false;
  //  }

  //if (!processedEvent)
  //  {
  //  processedEvent = this->ProcessButtonClickEvent(eventData);
  //  }

  //if (processedEvent)
  //  {
  //  // invoke interaction event for compatibility with pre-camera-widget
  //  // behavior of vtk event processing.  This enables events to pass
  //  // through the qMRMLThreeDView to the cameraNode
  //  // for broadcast to other cameras
  //  vtkRenderWindowInteractor* interactor = this->Renderer->GetRenderWindow()->GetInteractor();
  //  interactor->InvokeEvent(vtkCommand::InteractionEvent);
  //  }

  //return processedEvent;
return false;
}

#include <vtkEventBroker.h>
////---------------------------------------------------------------------------
//void vtkMRMLFocusWidget::UpdateFromMRMLScene()
//{
//  // UpdateFromMRML will be executed only if there has been some actions
//  // during the import that requested it (don't call
//  // SetUpdateFromMRMLRequested(1) here, it should be done somewhere else
//  // maybe in OnMRMLSceneNodeAddedEvent, OnMRMLSceneNodeRemovedEvent or
//  // OnMRMLDisplayableModelNodeModifiedEvent).
//
//  vtkEventBroker* broker = vtkEventBroker::GetInstance();
//  if (this->Internal->SelectionNode)
//    {
//    broker->RemoveObservations(this->Internal->SelectionNode,
//      vtkCommand::ModifiedEvent,
//      this, this->GetMRMLNodesCallbackCommand());
//    }
//
//  this->Internal->SelectionNode = this->GetSelectionNode();
//
//  broker->AddObservation(this->Internal->SelectionNode,
//    vtkCommand::ModifiedEvent,
//    this, this->GetMRMLNodesCallbackCommand());
//
//  this->UpdateFromMRML();
//}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::ProcessMRMLNodesEvents(vtkObject* caller,
  unsigned long event,
  void* callData)
{
  if (this->GetMRMLScene() == nullptr)
    {
    return;
    }

  if (vtkMRMLNode::SafeDownCast(caller))
    {
    this->UpdateFromMRML();
    }
  else if (vtkProp::SafeDownCast(caller))
    {
    this->UpdateActor(vtkProp::SafeDownCast(caller));
    }
  else if (vtkCoordinate::SafeDownCast(caller))
    {
    this->UpdateActors();
    }

  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double& closestDistance2)
{
  vtkMRMLSelectionNode* selectionNode = this->GetSelectionNode();
  if (eventData->GetType() == vtkCommand::KeyPressEvent && eventData->GetKeyCode() == 27
    && selectionNode && selectionNode->GetFocusNodeID())
    {
    closestDistance2 = 1e10; // Allow other widgets to have precedence.
    return true;
    }
  return false;
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLSelectionNode* selectionNode = this->GetSelectionNode();
  if (eventData->GetType() == vtkCommand::KeyPressEvent && eventData->GetKeyCode() == 27
    && selectionNode && selectionNode->GetFocusNodeID())
    {
    vtkMRMLNode* focusNode = selectionNode->GetScene()->GetNodeByID(selectionNode->GetFocusNodeID());
    this->GetSelectionNode()->SetFocusNodeID(nullptr);
    return true;
    }
  return false;
}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::UpdateFromMRML()
{
  this->Internal->RendererOutline->RemoveAllViewProps();

  this->Internal->RemoveFocusedNodeObservers();
  this->Internal->DisplayableNodes.clear();

  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
  const char* focusNodeID = selectionNode ? selectionNode->GetFocusNodeID() : nullptr;
  vtkMRMLDisplayableNode* focusedNode =
    vtkMRMLDisplayableNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(focusNodeID));

  vtkRenderer* renderer = this->GetRenderer();
  if (!selectionNode || !renderer || !focusedNode || focusedNode->GetNumberOfDisplayNodes() == 0)
    {
    this->SetUpdateFromMRMLRequested(false);
    this->RequestRender();
    return;
    }

  int RENDERER_LAYER = 1;
  if (renderer->GetRenderWindow()->GetNumberOfLayers() < RENDERER_LAYER + 1)
    {
    renderer->GetRenderWindow()->SetNumberOfLayers(RENDERER_LAYER + 1);
    }

  this->Internal->ROIGlowPass->SetOutlineIntensity(selectionNode->GetFocusedHighlightStrength());
  this->Internal->RendererOutline->SetLayer(RENDERER_LAYER);

  this->Internal->DisplayableNodes.push_back(focusedNode);
  this->Internal->AddFocusedNodeObservers();

  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (vtkProp* oldActor : this->Internal->OriginalActors)
    {
    if (!oldActor)
      {
      continue;
      }
    broker->RemoveObservations(oldActor, vtkCommand::ModifiedEvent,
      this, this->GetMRMLNodesCallbackCommand());

    vtkActor2D* oldActor2D = vtkActor2D::SafeDownCast(oldActor);
    if (oldActor2D)
      {
      // Need to update copied actors when the position of the 2D actor changes
      broker->RemoveObservations(oldActor2D->GetPositionCoordinate(),
        vtkCommand::ModifiedEvent,
        this, this->GetMRMLNodesCallbackCommand());
      }
    }

  this->Internal->OriginalActors.clear();

  std::vector<vtkMRMLDisplayNode*> displayNodes;
  for (int i = 0; i < focusedNode->GetNumberOfDisplayNodes(); ++i)
    {
    vtkMRMLDisplayNode* displayNode = focusedNode->GetNthDisplayNode(i);
    if (!displayNode)
      {
      continue;
      }
    displayNodes.push_back(displayNode);
    }

  vtkNew<vtkPropCollection> focusNodeActors;
  vtkMRMLDisplayableManagerGroup* group = this->GetMRMLDisplayableManagerGroup();
  for (vtkMRMLDisplayNode* displayNode : displayNodes)
    {
    for (int i = 0; i < group->GetDisplayableManagerCount(); ++i)
      {
      vtkMRMLAbstractDisplayableManager* displayableManager = group->GetNthDisplayableManager(i);
      displayableManager->GetActorsByID(focusNodeActors, displayNode->GetID(),
        selectionNode->GetFocusedComponentType(), selectionNode->GetFocusedComponentIndex());
      }
    }

  if (focusNodeActors->GetNumberOfItems() == 0)
    {
    // The focused node has no actors for the focused component type/index.
    this->SetUpdateFromMRMLRequested(false);
    this->RequestRender();
    return;
    }

  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> newOriginalToCopyActors;

  vtkSmartPointer<vtkProp> prop = nullptr;
  vtkCollectionSimpleIterator it = nullptr;
  for (focusNodeActors->InitTraversal(it); prop = focusNodeActors->GetNextProp(it);)
    {
    if (!prop->GetVisibility())
      {
      // Ignore actors that are not visible.
      continue;
      }

    vtkSmartPointer<vtkProp> newProp = this->Internal->OriginalToCopyActors[prop];
    if (!newProp)
      {
      newProp = vtkSmartPointer<vtkProp>::Take(prop->NewInstance());
      }

    this->Internal->OriginalActors.push_back(prop);
    newOriginalToCopyActors[prop] = newProp;
    this->Internal->RendererOutline->AddViewProp(newProp);

    broker->AddObservation(prop,
      vtkCommand::ModifiedEvent,
      this, this->GetMRMLNodesCallbackCommand());

    vtkActor2D* copyActor2D = vtkActor2D::SafeDownCast(prop);
    if (copyActor2D)
      {
      broker->AddObservation(copyActor2D->GetPositionCoordinate(),
        vtkCommand::ModifiedEvent,
        this, this->GetMRMLNodesCallbackCommand());
      }
    }
  this->Internal->OriginalToCopyActors = newOriginalToCopyActors;

  this->UpdateActors();

  if (!renderer->HasViewProp(this->Internal->CornerROIActor))
    {
    renderer->AddActor(this->Internal->CornerROIActor);
    }

  this->Internal->RendererOutline->SetActiveCamera(renderer->GetActiveCamera());
  if (!renderer->GetRenderWindow()->HasRenderer(this->Internal->RendererOutline))
    {
    renderer->GetRenderWindow()->AddRenderer(this->Internal->RendererOutline);
    }
  this->SetUpdateFromMRMLRequested(false);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::UpdateActors()
{
  for (auto prop : this->Internal->OriginalActors)
    {
    if (!prop)
      {
      continue;
      }
    this->UpdateActor(prop);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::UpdateActor(vtkProp* originalProp)
{
  vtkProp* copyProp = this->Internal->OriginalToCopyActors[originalProp];
  if (!copyProp)
    {
    return;
    }

  // Copy the properties of the original actor to the duplicate one
  copyProp->ShallowCopy(originalProp);

  vtkActor* copyActor = vtkActor::SafeDownCast(copyProp);
  if (copyActor)
    {
    copyActor->SetTexture(nullptr);

    // Make the actor flat. This generates a better outline.
    vtkSmartPointer<vtkProperty> copyProperty = vtkSmartPointer<vtkProperty>::Take(copyActor->GetProperty()->NewInstance());
    copyProperty->DeepCopy(copyActor->GetProperty());
    copyProperty->SetLighting(false);
    copyProperty->SetColor(1.0, 1.0, 1.0);
    copyProperty->SetOpacity(1.0);
    copyActor->SetProperty(copyProperty);
    }

  vtkVolume* copyVolume = vtkVolume::SafeDownCast(copyProp);
  if (copyVolume)
    {
    vtkNew<vtkColorTransferFunction> colorTransferFunction;
    colorTransferFunction->AddRGBPoint(0, 1.0, 1.0, 1.0);

    vtkSmartPointer<vtkVolumeProperty> copyProperty = vtkSmartPointer<vtkVolumeProperty>::Take(copyVolume->GetProperty()->NewInstance());
    copyProperty->DeepCopy(copyVolume->GetProperty());
    copyProperty->SetDiffuse(0.0);
    copyProperty->SetAmbient(1.0);
    copyProperty->ShadeOff();
    copyProperty->SetColor(colorTransferFunction);
    copyVolume->SetProperty(copyProperty);
    }

  vtkActor2D* copyActor2D = vtkActor2D::SafeDownCast(copyProp);
  if (copyActor2D)
    {
    vtkSmartPointer<vtkProperty2D> copyProperty2D = vtkSmartPointer<vtkProperty2D>::Take(copyActor2D->GetProperty()->NewInstance());
    copyProperty2D->DeepCopy(copyActor2D->GetProperty());
    copyProperty2D->SetColor(1.0, 1.0, 1.0);
    copyActor2D->SetProperty(copyProperty2D);
    }

  vtkLabelPlacementMapper* oldLabelMapper = copyActor2D ? vtkLabelPlacementMapper::SafeDownCast(copyActor2D->GetMapper()) : nullptr;
  if (oldLabelMapper)
    {
    // TODO: Workaround for markups widgets in order to modify text property for control point labels.

    vtkPointSetToLabelHierarchy* oldPointSetInput = vtkPointSetToLabelHierarchy::SafeDownCast(oldLabelMapper->GetInputAlgorithm());
    if (oldPointSetInput)
      {
      vtkSmartPointer<vtkLabelPlacementMapper> newLabelMapper = vtkSmartPointer<vtkLabelPlacementMapper>::Take(oldLabelMapper->NewInstance());
      newLabelMapper->ShallowCopy(oldLabelMapper);

      vtkSmartPointer<vtkPointSetToLabelHierarchy> newPointSetInput = vtkSmartPointer<vtkPointSetToLabelHierarchy>::Take(oldPointSetInput->NewInstance());
      newPointSetInput->SetInputData(oldPointSetInput->GetInput());
      newPointSetInput->SetLabelArrayName("labels");
      newPointSetInput->SetPriorityArrayName("priority");

      vtkSmartPointer<vtkTextProperty> textProperty = vtkSmartPointer<vtkTextProperty>::Take(newPointSetInput->GetTextProperty()->NewInstance());
      textProperty->ShallowCopy(newPointSetInput->GetTextProperty());
      textProperty->SetBackgroundRGBA(1.0, 1.0, 1.0, 1.0);
      newPointSetInput->SetTextProperty(textProperty);

      newLabelMapper->SetInputConnection(newPointSetInput->GetOutputPort());

      copyActor2D->SetMapper(newLabelMapper);
      }
    }

  vtkTextActor* copyTextActor = vtkTextActor::SafeDownCast(copyProp);
  if (copyTextActor)
    {
    // TODO: Outline is not large enough if background is fully transparent.
    vtkSmartPointer<vtkTextProperty> copyTextProperty = vtkSmartPointer<vtkTextProperty>::Take(copyTextActor->GetTextProperty()->NewInstance());
    copyTextProperty->ShallowCopy(copyTextActor->GetTextProperty());
    copyTextProperty->SetBackgroundRGBA(1.0, 1.0, 1.0, 1.0);
    copyTextActor->SetTextProperty(copyTextProperty);
    }
}
