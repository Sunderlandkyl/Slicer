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

// VTK includes
#include <vtkCallbackCommand.h>
#include <vtkEvent.h>

/// MRML includes
#include <vtkMRMLAbstractLogic.h>
#include <vtkEventBroker.h>
#include <vtkMRMLLayoutNode.h>
#include <vtkObserverManager.h>
#include <vtkMRMLSelectionNode.h>

// MRMLDM includes
#include "vtkMRMLFocusWidget.h"
#include "vtkMRMLInteractionEventData.h"

vtkStandardNewMacro(vtkMRMLFocusWidget);

//---------------------------------------------------------------------------
vtkMRMLFocusWidget::vtkMRMLFocusWidget()
{
  this->MRMLNodesObserverManager->AssignOwner(this);
  this->MRMLNodesObserverManager->GetCallbackCommand()->SetClientData(reinterpret_cast<void*> (this));
  this->MRMLNodesObserverManager->GetCallbackCommand()->SetCallback(vtkMRMLFocusWidget::ProcessMRMLNodesEvents);

  this->SetKeyboardEventTranslation(WidgetStateFocus, vtkEvent::NoModifier, 0, 0, "Escape", WidgetEventCancelFocus);
}

//---------------------------------------------------------------------------
vtkMRMLFocusWidget::~vtkMRMLFocusWidget()
{
}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &distance2)
{
  unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);
  if (widgetEvent == WidgetEventCancelFocus)
  {
    distance2 = 1e10; // we can process this event but we let more specific widgets claim it
    return true;
  }
  return false;
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);

  bool processedEvent = false;
  switch (widgetEvent)
  {
    case WidgetEventCancelFocus:
      processedEvent = this->ProcessCancelFocusEvent(eventData);
      break;
    default:
      processedEvent = false;
  }
  return processedEvent;
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusWidget::ProcessCancelFocusEvent(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLSelectionNode* selectionNode = this->GetSelectionNode();
  if (!selectionNode)
  {
    return false;
  }

  MRMLNodeModifyBlocker blocker(selectionNode);
  selectionNode->SetFocusNodeID(nullptr);
  selectionNode->RemoveAllSoftFocus();
  return true;
}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::ProcessMRMLNodesEvents(vtkObject* caller,
  unsigned long event, void* clientData, void* callData)
{
  vtkMRMLFocusWidget* self = reinterpret_cast<vtkMRMLFocusWidget*>(clientData);
  vtkMRMLSelectionNode* callerSelectionNode = vtkMRMLSelectionNode::SafeDownCast(caller);
  if (callerSelectionNode)
  {
    bool focused = (callerSelectionNode->GetFocusNodeID() && strcmp(callerSelectionNode->GetFocusNodeID(), "") != 0)
      || callerSelectionNode->GetNumberOfNodeReferences(callerSelectionNode->GetSoftFocusNodeReferenceRole()) > 0;
    self->SetWidgetState(focused ? WidgetStateFocus : WidgetStateIdle);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusWidget::SetSelectionNode(vtkMRMLSelectionNode* selectionNode)
{
  if (this->SelectionNode == selectionNode)
  {
    return;
  }

  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  if (this->SelectionNode)
  {
    vtkUnObserveMRMLNodeMacro(this->SelectionNode);
  }

  this->SelectionNode = selectionNode;

  if (this->SelectionNode)
  {
    vtkNew<vtkIntArray> events;
    events->InsertNextValue(vtkMRMLSelectionNode::FocusNodeIDChangedEvent);
    vtkObserveMRMLNodeEventsMacro(this->SelectionNode, events);
  }
}

//---------------------------------------------------------------------------
vtkMRMLSelectionNode* vtkMRMLFocusWidget::GetSelectionNode()
{
  return this->SelectionNode;
}
