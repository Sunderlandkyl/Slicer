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

// MRMLDisplayableManager includes
#include "vtkMRMLApplicationLogic.h"
#include "vtkMRMLFocusDisplayableManager.h"
#include <vtkMRMLFocusWidget.h>
#include <vtkMRMLInteractionEventData.h>

// MRML/Slicer includes
#include <vtkEventBroker.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLDisplayableManagerGroup.h>
#include <vtkMRMLScene.h>

// VTK includes
#include <vtkAbstractVolumeMapper.h>
#include <vtkActor2D.h>
#include <vtkColorTransferFunction.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkLabelPlacementMapper.h>
#include <vtkMapper.h>
#include <vtkMapper2D.h>
#include <vtkOutlineGlowPass.h>
#include <vtkPointSetToLabelHierarchy.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
#include <vtkRenderStepsPass.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include<vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkVolumeProperty.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLFocusDisplayableManager);

//---------------------------------------------------------------------------
class vtkMRMLFocusDisplayableManager::vtkInternal
{
public:
  vtkInternal(vtkMRMLFocusDisplayableManager* external);
  ~vtkInternal();

  void RemoveFocusedNodeObservers();
  void AddFocusedNodeObservers();

public:
  vtkMRMLFocusDisplayableManager* External;

  vtkNew<vtkMRMLFocusWidget> FocusWidget;

  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> DisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> OriginalActors;
  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> OriginalToCopyActors;


};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::vtkInternal(vtkMRMLFocusDisplayableManager* external)
  : External(external)
{
}

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::~vtkInternal()
{
  this->FocusWidget->SetSelectionNode(nullptr);
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::AddFocusedNodeObservers()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (vtkMRMLDisplayableNode* displayableNode : this->DisplayableNodes)
    {
    if (!displayableNode)
      {
      continue;
      }

    vtkIntArray* contentModifiedEvents = displayableNode->GetContentModifiedEvents();
    for (int i = 0; i < contentModifiedEvents->GetNumberOfValues(); ++i)
      {
      broker->AddObservation(displayableNode,
        contentModifiedEvents->GetValue(i),
        this->External, this->External->GetMRMLNodesCallbackCommand());
      }

    broker->AddObservation(displayableNode,
      vtkCommand::ModifiedEvent,
      this->External, this->External->GetMRMLNodesCallbackCommand());

    broker->AddObservation(displayableNode,
      vtkMRMLTransformableNode::TransformModifiedEvent,
      this->External, this->External->GetMRMLNodesCallbackCommand());

    broker->AddObservation(displayableNode,
      vtkMRMLDisplayableNode::DisplayModifiedEvent,
      this->External, this->External->GetMRMLNodesCallbackCommand());
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::RemoveFocusedNodeObservers()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (vtkMRMLDisplayableNode* displayableNode : this->DisplayableNodes)
    {
    if (!displayableNode)
      {
      continue;
      }
    vtkIntArray* contentModifiedEvents = displayableNode->GetContentModifiedEvents();
    for (int i = 0; i < contentModifiedEvents->GetNumberOfValues(); ++i)
      {
      broker->RemoveObservations(displayableNode,
        contentModifiedEvents->GetValue(i),
        this->External, this->External->GetMRMLNodesCallbackCommand());
      }
    broker->RemoveObservations(displayableNode,
      vtkCommand::ModifiedEvent,
      this->External, this->External->GetMRMLNodesCallbackCommand());

    broker->RemoveObservations(displayableNode,
      vtkMRMLTransformableNode::TransformModifiedEvent,
      this->External, this->External->GetMRMLNodesCallbackCommand());

    broker->RemoveObservations(displayableNode,
      vtkMRMLDisplayableNode::DisplayModifiedEvent,
      this->External, this->External->GetMRMLNodesCallbackCommand());
    }
}

//---------------------------------------------------------------------------
// vtkMRMLFocusDisplayableManager methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkMRMLFocusDisplayableManager()
{
  this->Internal = new vtkInternal(this);
}

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::~vtkMRMLFocusDisplayableManager()
{
  delete this->Internal;
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::PrintSelf(ostream& os, vtkIndent indent)
{
  this->vtkObject::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateFromMRMLScene()
{
  // UpdateFromMRML will be executed only if there has been some actions
  // during the import that requested it (don't call
  // SetUpdateFromMRMLRequested(1) here, it should be done somewhere else
  // maybe in OnMRMLSceneNodeAddedEvent, OnMRMLSceneNodeRemovedEvent or
  // OnMRMLDisplayableModelNodeModifiedEvent).

  this->SetAndObserveSelectionNode(vtkMRMLSelectionNode::SafeDownCast(this->GetMRMLScene()->GetFirstNodeByClass("vtkMRMLSelectionNode")));
  this->UpdateFromMRML();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::ProcessMRMLNodesEvents(vtkObject* caller,
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

  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusDisplayableManager::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double& closestDistance2)
{
  return this->Internal->FocusWidget->CanProcessInteractionEvent(eventData, closestDistance2);
}

//---------------------------------------------------------------------------
bool vtkMRMLFocusDisplayableManager::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  return this->Internal->FocusWidget->ProcessInteractionEvent(eventData);
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateFromMRML()
{

//  this->Internal->RendererOutline->RemoveAllViewProps();
//
//  this->Internal->RemoveFocusedNodeObservers();
//  this->Internal->DisplayableNodes.clear();
//
//  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
//  const char* focusNodeID = selectionNode ? selectionNode->GetFocusNodeID() : nullptr;
//  vtkMRMLDisplayableNode* focusedNode =
//    vtkMRMLDisplayableNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(focusNodeID));
//
//  vtkRenderer* renderer = this->GetRenderer();
//  if (!selectionNode || !renderer || !focusedNode || focusedNode->GetNumberOfDisplayNodes() == 0)
//    {
//    this->SetUpdateFromMRMLRequested(false);
//    this->RequestRender();
//    return;
//    }
//
//  int RENDERER_LAYER = 1;
//  if (renderer->GetRenderWindow()->GetNumberOfLayers() < RENDERER_LAYER + 1)
//    {
//    renderer->GetRenderWindow()->SetNumberOfLayers(RENDERER_LAYER + 1);
//    }
//
//  this->Internal->ROIGlowPass->SetOutlineIntensity(selectionNode->GetFocusedHighlightStrength());
//  this->Internal->RendererOutline->SetLayer(RENDERER_LAYER);
//
//  this->Internal->DisplayableNodes.push_back(focusedNode);
//  this->Internal->AddFocusedNodeObservers();
//
//  vtkEventBroker* broker = vtkEventBroker::GetInstance();
//
//  for (vtkProp* oldActor : this->Internal->OriginalActors)
//    {
//    if (!oldActor)
//      {
//      continue;
//      }
//    broker->RemoveObservations(oldActor, vtkCommand::ModifiedEvent,
//      this, this->GetMRMLNodesCallbackCommand());
//
//    vtkActor2D* oldActor2D = vtkActor2D::SafeDownCast(oldActor);
//    if (oldActor2D)
//      {
//      // Need to update copied actors when the position of the 2D actor changes
//      broker->RemoveObservations(oldActor2D->GetPositionCoordinate(),
//        vtkCommand::ModifiedEvent,
//        this, this->GetMRMLNodesCallbackCommand());
//      }
//    }
//
//  this->Internal->OriginalActors.clear();
//
//  std::vector<vtkMRMLDisplayNode*> displayNodes;
//  for (int i = 0; i < focusedNode->GetNumberOfDisplayNodes(); ++i)
//    {
//    vtkMRMLDisplayNode* displayNode = focusedNode->GetNthDisplayNode(i);
//    if (!displayNode)
//      {
//      continue;
//      }
//    displayNodes.push_back(displayNode);
//    }
//
//  vtkNew<vtkPropCollection> focusNodeActors;
//  vtkMRMLDisplayableManagerGroup* group = this->GetMRMLDisplayableManagerGroup();
//  for (vtkMRMLDisplayNode* displayNode : displayNodes)
//    {
//    for (int i = 0; i < group->GetDisplayableManagerCount(); ++i)
//      {
//      vtkMRMLAbstractDisplayableManager* displayableManager = group->GetNthDisplayableManager(i);
//      displayableManager->GetActorsByID(focusNodeActors, displayNode->GetID(),
//        selectionNode->GetFocusedComponentType(), selectionNode->GetFocusedComponentIndex());
//      }
//    }
//
//  if (focusNodeActors->GetNumberOfItems() == 0)
//    {
//    // The focused node has no actors for the focused component type/index.
//    this->SetUpdateFromMRMLRequested(false);
//    this->RequestRender();
//    return;
//    }
//
//  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> newOriginalToCopyActors;
//
//  vtkSmartPointer<vtkProp> prop = nullptr;
//  vtkCollectionSimpleIterator it = nullptr;
//  for (focusNodeActors->InitTraversal(it); prop = focusNodeActors->GetNextProp(it);)
//    {
//    if (!prop->GetVisibility())
//      {
//      // Ignore actors that are not visible.
//      continue;
//      }
//
//    vtkSmartPointer<vtkProp> newProp = this->Internal->OriginalToCopyActors[prop];
//    if (!newProp)
//      {
//      newProp = vtkSmartPointer<vtkProp>::Take(prop->NewInstance());
//      }
//
//    this->Internal->OriginalActors.push_back(prop);
//    newOriginalToCopyActors[prop] = newProp;
//    this->Internal->RendererOutline->AddViewProp(newProp);
//
//    broker->AddObservation(prop,
//      vtkCommand::ModifiedEvent,
//      this, this->GetMRMLNodesCallbackCommand());
//
//    vtkActor2D* newActor2D = vtkActor2D::SafeDownCast(prop);
//    if (newActor2D)
//      {
//      broker->AddObservation(newActor2D->GetPositionCoordinate(),
//        vtkCommand::ModifiedEvent,
//        this, this->GetMRMLNodesCallbackCommand());
//      }
//
//    }
//  this->Internal->OriginalToCopyActors = newOriginalToCopyActors;
//
//  this->UpdateActors();
//
//  this->Internal->RendererOutline->SetActiveCamera(renderer->GetActiveCamera());
//  if (!renderer->GetRenderWindow()->HasRenderer(this->Internal->RendererOutline))
//    {
//    renderer->GetRenderWindow()->AddRenderer(this->Internal->RendererOutline);
//    }
//  this->SetUpdateFromMRMLRequested(false);
//  this->RequestRender();
}
//
////---------------------------------------------------------------------------
//void vtkMRMLFocusDisplayableManager::UpdateSelectionNode()
//{
//  vtkMRMLScene* mrmlScene = this->GetMRMLScene();
//  if (!mrmlScene)
//    {
//    vtkErrorMacro("UpdateSelectionNode: No MRML scene");
//    return;
//    }
//
//  vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(mrmlScene->GetFirstNodeByClass("vtkMRMLSelectionNode"));
//  if (!selectionNode)
//    {
//    vtkErrorMacro("UpdateSelectionNode: No selection node");
//    return;
//    }
//
//  this->SetAndObserveSelectionNode(selectionNode);
//}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::SetAndObserveSelectionNode(vtkMRMLSelectionNode* newSelectionNode)
{
  vtkMRMLSelectionNode* oldSelectionNode = this->Internal->FocusWidget->GetSelectionNode();
  if (oldSelectionNode)
    {
    vtkUnObserveMRMLNodeMacro(oldSelectionNode);
    }
  this->Internal->FocusWidget->SetSelectionNode(newSelectionNode);
  if (newSelectionNode)
    {
    vtkObserveMRMLNodeMacro(newSelectionNode);
    }
}
