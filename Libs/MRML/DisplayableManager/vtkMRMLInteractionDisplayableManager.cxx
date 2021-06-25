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

#include "vtkMRMLInteractionDisplayableManager.h"

#include "vtkMRMLAbstractSliceViewDisplayableManager.h"

// MRMLDisplayableManager includes
#include <vtkMRMLDisplayableManagerGroup.h>
#include <vtkMRMLInteractionEventData.h>
#include <vtkMRMLModelDisplayableManager.h>

// MRML includes

#include <vtkEventBroker.h>
#include <vtkMRMLApplicationLogic.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLFolderDisplayNode.h>
#include <vtkMRMLInteractionNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLSliceLogic.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLTransformNode.h>
#include <vtkMRMLViewNode.h>

// VTK includes
#include <vtkAbstractWidget.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkEvent.h>
#include <vtkGeneralTransform.h>
#include <vtkMath.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPropCollection.h>
#include <vtkProperty2D.h>
#include <vtkRendererCollection.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSphereSource.h>
#include <vtkTextProperty.h>
#include <vtkWidgetRepresentation.h>

// STD includes
#include <algorithm>
#include <map>
#include <vector>
#include <sstream>
#include <string>

typedef void (*fp)();

#define NUMERIC_ZERO 0.001

//---------------------------------------------------------------------------
vtkStandardNewMacro (vtkMRMLInteractionDisplayableManager);

//---------------------------------------------------------------------------
// vtkMRMLInteractionDisplayableManager methods

//---------------------------------------------------------------------------
vtkMRMLInteractionDisplayableManager::vtkMRMLInteractionDisplayableManager()
{
  this->DisableInteractorStyleEventsProcessing = 0;
  this->LastActiveWidget = nullptr;
  this->DisplayableEvents = vtkSmartPointer<vtkIdTypeArray>::New();
  this->DisplayableEvents->InsertNextValue(vtkCommand::ModifiedEvent);
  this->DisplayableEvents->InsertNextValue(vtkMRMLTransformableNode::TransformModifiedEvent);
  this->DisplayableEvents->InsertNextValue(vtkMRMLDisplayableNode::DisplayModifiedEvent);
}

//---------------------------------------------------------------------------
vtkMRMLInteractionDisplayableManager::~vtkMRMLInteractionDisplayableManager()
{
  this->DisableInteractorStyleEventsProcessing = 0;
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "DisableInteractorStyleEventsProcessing = " << this->DisableInteractorStyleEventsProcessing << std::endl;
  if (this->SliceNode &&
      this->SliceNode->GetID())
    {
    os << indent << "Slice node id = " << this->SliceNode->GetID() << std::endl;
    }
  else
    {
    os << indent << "No slice node" << std::endl;
    }
}

//---------------------------------------------------------------------------
vtkMRMLInteractionWidget* vtkMRMLInteractionDisplayableManager::CreateWidget(vtkMRMLDisplayNode* displayNode)
{
  if (!displayNode)
    {
    return nullptr;
    }

  // Create a widget of the associated type if the node matches the registered nodes
  vtkMRMLInteractionWidget* widget = vtkMRMLInteractionWidget::New();

  // If the widget was successfully created
  if (widget)
    {
    vtkMRMLAbstractViewNode* viewNode = vtkMRMLAbstractViewNode::SafeDownCast(this->GetMRMLDisplayableNode());
    vtkRenderer* renderer = this->GetRenderer();
    widget->SetMRMLApplicationLogic(this->GetMRMLApplicationLogic());
    widget->CreateDefaultRepresentation(displayNode, viewNode, renderer);
    }

  return widget;
}

//---------------------------------------------------------------------------
vtkMRMLSliceNode * vtkMRMLInteractionDisplayableManager::GetMRMLSliceNode()
{
  return vtkMRMLSliceNode::SafeDownCast(this->GetMRMLDisplayableNode());
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::Is2DDisplayableManager()
{
  return this->GetMRMLSliceNode() != nullptr;
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::RequestRender()
{
  if (!this->GetMRMLScene())
    {
    return;
    }

  if (!this->GetMRMLScene()->IsBatchProcessing())
    {
    this->Superclass::RequestRender();
    }

  this->SetUpdateFromMRMLRequested(false);
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::SetMRMLSceneInternal(vtkMRMLScene* newScene)
{
  Superclass::SetMRMLSceneInternal(newScene);

  // after a new scene got associated, we want to make sure everything old is gone
  this->OnMRMLSceneEndClose();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager
::ProcessMRMLNodesEvents(vtkObject *caller, unsigned long event, void *callData)
{
  Superclass::ProcessMRMLNodesEvents(caller, event, callData);

  vtkMRMLDisplayNode* displayNode = vtkMRMLDisplayNode::SafeDownCast(caller);
  vtkMRMLDisplayableNode* displayableNode = vtkMRMLDisplayableNode::SafeDownCast(caller);
  if (!displayNode && displayableNode)
    {
    displayNode = displayableNode->GetDisplayNode();
    }

  if (displayNode)
    {
    bool renderRequested = false;

    if (this->InteractionWidgets.find(displayNode) != this->InteractionWidgets.end())
      {
      vtkMRMLInteractionWidget* widget = this->InteractionWidgets[displayNode];
      if (widget)
        {
        widget->UpdateFromMRML(displayNode, event, callData);
        if (widget->GetNeedToRender())
          {
          renderRequested = true;
          widget->NeedToRenderOff();
          }
        }
      }
    if (renderRequested)
      {
      this->RequestRender();
      }
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnMRMLSceneEndClose()
{
  this->UpdateFromMRMLScene();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnMRMLSceneEndImport()
{
  this->UpdateFromMRMLScene();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::UpdateFromMRMLScene()
{
  this->RemoveAllDisplayableNodes();
  this->RemoveAllDisplayNodes();

  if (!this->GetMRMLScene())
    {
    return;
    }

  std::vector<vtkMRMLNode*> displayNodes;
  this->GetMRMLScene()->GetNodesByClass("vtkMRMLDisplayNode", displayNodes);
  for (auto node : displayNodes)
    {
    vtkMRMLDisplayNode* displayNode = vtkMRMLDisplayNode::SafeDownCast(node);
    this->AddDisplayNode(displayNode);
    }

  std::vector<vtkMRMLNode*> displayableNodes;
  this->GetMRMLScene()->GetNodesByClass("vtkMRMLDisplayableNode", displayableNodes);
  for (auto node : displayableNodes)
    {
    vtkMRMLDisplayableNode* displayableNode = vtkMRMLDisplayableNode::SafeDownCast(node);
    this->AddDisplayableNode(displayableNode);
    }

  this->SetUpdateFromMRMLRequested(true);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  Superclass::OnMRMLSceneNodeAdded(node);
  if (!node)
    {
    return;
    }

  vtkMRMLDisplayableNode* displayableNode = vtkMRMLDisplayableNode::SafeDownCast(node);
  vtkMRMLDisplayNode* displayNode = vtkMRMLDisplayNode::SafeDownCast(node);
  if (!displayableNode && !displayNode)
    {
    return;
    }

  this->SetUpdateFromMRMLRequested(true);

  // if the scene is still updating, jump out
  if (this->GetMRMLScene() && this->GetMRMLScene()->IsBatchProcessing())
    {
    return;
    }

  if (displayableNode)
    {
    this->AddDisplayableNode(displayableNode);
    }
  else if (displayNode)
    {
    this->AddDisplayNode(displayNode);
    }

  // and render again
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  Superclass::OnMRMLSceneNodeRemoved(node);

  vtkMRMLDisplayableNode* displayableNode = vtkMRMLDisplayableNode::SafeDownCast(node);
  vtkMRMLDisplayNode* displayNode = vtkMRMLDisplayNode::SafeDownCast(node);
  if (!displayableNode && !displayNode)
    {
    return;
    }

  this->SetUpdateFromMRMLRequested(true);

  // if the scene is still updating, jump out
  if (this->GetMRMLScene() && this->GetMRMLScene()->IsBatchProcessing())
    {
    return;
    }

  if (displayableNode)
    {
    this->RemoveDisplayableNode(displayableNode);
    }
  else if (displayNode)
    {
    this->RemoveDisplayNode(displayNode);
    }

  // and render again
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::AddDisplayNode(vtkMRMLDisplayNode* displayNode)
{
  if (!displayNode)
    {
    return;
    }

  this->DisplayNodes.insert(displayNode);

  if (this->InteractionWidgets.find(displayNode) == this->InteractionWidgets.end())
    {
    vtkSmartPointer<vtkMRMLInteractionWidget> widget = vtkSmartPointer<vtkMRMLInteractionWidget>::Take(this->CreateWidget(displayNode));
    if (widget)
      {
      this->InteractionWidgets[displayNode] = widget;
      this->RequestRender();
      }
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::RemoveDisplayNode(vtkMRMLDisplayNode* displayNode)
{
  if (!displayNode)
    {
    return;
    }

  if (this->DisplayNodes.find(displayNode) != this->DisplayNodes.end())
    {
    this->DisplayNodes.erase(displayNode);
    this->RequestRender();
    }

  if (this->InteractionWidgets.find(displayNode) != this->InteractionWidgets.end())
    {
    this->InteractionWidgets.erase(displayNode);
    this->RequestRender();
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::AddDisplayableNode(vtkMRMLDisplayableNode* displayableNode)
{
  if (!displayableNode)
    {
    return;
    }

  this->AddObservations(displayableNode);
  this->DisplayableNodes.insert(displayableNode);
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::RemoveAllDisplayableNodes()
{
  std::set<vtkSmartPointer<vtkMRMLDisplayableNode>> displayableNodes = this->DisplayableNodes;
  for (vtkMRMLDisplayableNode* displayableNode : displayableNodes)
    {
    this->RemoveDisplayableNode(displayableNode);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::RemoveAllDisplayNodes()
{
  std::set<vtkSmartPointer<vtkMRMLDisplayNode>> displayNodes = this->DisplayNodes;
  for (auto displayNode : displayNodes)
    {
    this->RemoveDisplayNode(displayNode);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::RemoveDisplayableNode(vtkMRMLDisplayableNode* displayableNode)
{
  if (this->DisplayableNodes.find(displayableNode) != this->DisplayableNodes.end())
    {
    this->DisplayableNodes.erase(displayableNode);
    }
  this->RemoveObservations(displayableNode);
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::AddObservations(vtkMRMLDisplayableNode* displayableNode)
{
  if (!displayableNode)
    {
    return;
    }

  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  for (int i = 0; i < this->DisplayableEvents->GetNumberOfValues(); ++i)
    {
    vtkIdType eventId = this->DisplayableEvents->GetValue(i);
    if (broker->GetObservationExist(displayableNode, eventId,
      this, this->GetMRMLNodesCallbackCommand()))
      {
      continue;
      }
    broker->AddObservation(displayableNode, eventId, this, this->GetMRMLNodesCallbackCommand());
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::RemoveObservations(vtkMRMLDisplayableNode* displayableNode)
{
  if (!displayableNode)
    {
    return;
    }

  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  vtkEventBroker::ObservationVector observations;
  for (int i = 0; i < this->DisplayableEvents->GetNumberOfValues(); ++i)
    {
    vtkIdType eventId = this->DisplayableEvents->GetValue(i);
    if (broker->GetObservationExist(displayableNode, eventId,
      this, this->GetMRMLNodesCallbackCommand()))
      {
      continue;
      }
    observations = broker->GetObservations(
      displayableNode, eventId, this, this->GetMRMLNodesCallbackCommand());
    broker->RemoveObservations(observations);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller)
{
  Superclass::OnMRMLDisplayableNodeModifiedEvent(caller);

  vtkDebugMacro("OnMRMLDisplayableNodeModifiedEvent");

  if (!caller)
    {
    vtkErrorMacro("OnMRMLDisplayableNodeModifiedEvent: Could not get caller.");
    return;
    }

  vtkMRMLSliceNode * sliceNode = vtkMRMLSliceNode::SafeDownCast(caller);
  if (sliceNode)
    {
    // the associated renderWindow is a 2D SliceView
    // this is the entry point for all events fired by one of the three sliceviews
    // (e.g. change slice number, zoom etc.)

    // we remember that this instance of the displayableManager deals with 2D
    // this is important for widget creation etc. and save the actual SliceNode
    // because during Slicer startup the SliceViews fire events, it will be always set correctly
    this->SliceNode = sliceNode;

    // now we call the handle for specific sliceNode actions
    this->OnMRMLSliceNodeModifiedEvent();

    // and exit
    return;
    }

  vtkMRMLViewNode * viewNode = vtkMRMLViewNode::SafeDownCast(caller);
  if (viewNode)
    {
    // the associated renderWindow is a 3D View
    vtkDebugMacro("OnMRMLDisplayableNodeModifiedEvent: This displayableManager handles a ThreeD view.");
    return;
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnMRMLSliceNodeModifiedEvent()
{
  this->UpdateFromMRML();

  bool renderRequested = false;
  for (auto iteractionIt : this->InteractionWidgets)
    {
    vtkMRMLInteractionWidget* widget = iteractionIt.second;
    if (widget)
      {
      widget->UpdateFromMRML(this->SliceNode, vtkCommand::ModifiedEvent);
      if (widget->GetNeedToRender())
        {
        renderRequested = true;
        widget->NeedToRenderOff();
        }
      }
    }
  if (renderRequested)
    {
    this->RequestRender();
    }
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::OnInteractorStyleEvent(int eventid)
{
  Superclass::OnInteractorStyleEvent(eventid);
}

//---------------------------------------------------------------------------
/// Check if it is the correct displayableManager
//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::IsCorrectDisplayableManager()
{
  vtkMRMLSelectionNode *selectionNode = this->GetMRMLApplicationLogic()->GetSelectionNode();
  if (selectionNode == nullptr)
    {
    vtkErrorMacro ("IsCorrectDisplayableManager: No selection node in the scene.");
    return false;
    }

  return false;
}
//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::IsManageable(vtkMRMLNode* node)
{
  if (node == nullptr)
    {
    vtkErrorMacro("Is Manageable: invalid node.");
    return false;
    }

  return this->IsManageable(node->GetClassName());
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::IsManageable(const char* nodeClassName)
{
  if (nodeClassName == nullptr)
    {
   return false;
    }
  return false;
}

//---------------------------------------------------------------------------
vtkMRMLInteractionWidget* vtkMRMLInteractionDisplayableManager::FindClosestWidget(vtkMRMLInteractionEventData* callData, double& closestDistance2)
{
  vtkMRMLInteractionWidget* closestWidget = nullptr;
  closestDistance2 = VTK_DOUBLE_MAX;

  for (auto widgetIterator : this->InteractionWidgets)
    {
    vtkMRMLInteractionWidget* widget = widgetIterator.second;
    if (!widget)
      {
      continue;
      }
    double distance2FromWidget = VTK_DOUBLE_MAX;
    if (widget->CanProcessInteractionEvent(callData, distance2FromWidget))
      {
      if (!closestWidget || distance2FromWidget < closestDistance2)
        {
        closestDistance2 = distance2FromWidget;
        closestWidget = widget;
        }
      }
    }
  return closestWidget;
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &closestDistance2)
{
  // New point can be placed anywhere
  int eventid = eventData->GetType();

  if (eventid == vtkCommand::LeaveEvent && this->LastActiveWidget != nullptr)
    {
    if (this->LastActiveWidget->GetDisplayNode() && this->LastActiveWidget->GetDisplayNode()->GetActiveInteractionType() > vtkMRMLDisplayNode::InteractionNone)
      {
      // this widget has active component, therefore leave event is relevant
      closestDistance2 = 0.0;
      return this->LastActiveWidget;
      }
    }

  // Other interactions
  bool canProcess = (this->FindClosestWidget(eventData, closestDistance2) != nullptr);

  if (!canProcess && this->LastActiveWidget != nullptr
    && (eventid == vtkCommand::MouseMoveEvent || eventid == vtkCommand::Move3DEvent) )
    {
    // TODO: handle multiple contexts
    this->LastActiveWidget->Leave(eventData);
    this->LastActiveWidget = nullptr;
    }

  return canProcess;
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  if (this->GetDisableInteractorStyleEventsProcessing())
    {
    return false;
    }
  int eventid = eventData->GetType();

  if (eventid == vtkCommand::LeaveEvent)
    {
    if (this->LastActiveWidget != nullptr)
      {
      this->LastActiveWidget->Leave(eventData);
      this->LastActiveWidget = nullptr;
      }
    }

  // Find/create active widget
  vtkMRMLInteractionWidget* activeWidget = nullptr;
  double closestDistance2 = VTK_DOUBLE_MAX;
  activeWidget = this->FindClosestWidget(eventData, closestDistance2);

  // Deactivate previous widget
  if (this->LastActiveWidget != nullptr && this->LastActiveWidget != activeWidget)
    {
    this->LastActiveWidget->Leave(eventData);
    }
  this->LastActiveWidget = activeWidget;
  if (!activeWidget)
    {
    // deactivate widget if we move far from it
    if (eventid == vtkCommand::MouseMoveEvent && this->LastActiveWidget != nullptr)
      {
      this->LastActiveWidget->Leave(eventData);
      this->LastActiveWidget = nullptr;
      }
    return false;
    }

  // Pass on the interaction event to the active widget
  return activeWidget->ProcessInteractionEvent(eventData);
}

//---------------------------------------------------------------------------
int vtkMRMLInteractionDisplayableManager::GetCurrentInteractionMode()
{
  vtkMRMLInteractionNode *interactionNode = this->GetInteractionNode();
  if (!interactionNode)
    {
    return 0;
    }
  return interactionNode->GetCurrentInteractionMode();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::SetHasFocus(bool hasFocus)
{
  Superclass::SetHasFocus(hasFocus);
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::GetGrabFocus()
{
  return Superclass::GetGrabFocus();
}

//---------------------------------------------------------------------------
bool vtkMRMLInteractionDisplayableManager::GetInteractive()
{
  return Superclass::GetInteractive();
}

//---------------------------------------------------------------------------
int vtkMRMLInteractionDisplayableManager::GetMouseCursor()
{
  return Superclass::GetMouseCursor();
}

//---------------------------------------------------------------------------
void vtkMRMLInteractionDisplayableManager::ConvertDeviceToXYZ(double x, double y, double xyz[3])
{
  vtkMRMLAbstractSliceViewDisplayableManager::ConvertDeviceToXYZ(this->GetInteractor(), this->GetMRMLSliceNode(), x, y, xyz);
}
