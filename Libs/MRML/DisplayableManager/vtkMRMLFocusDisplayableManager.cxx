/*/*==============================================================================

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
#include "vtkMRMLFocusWidget.h"
#include <vtkMRMLInteractionEventData.h>

// MRML/Slicer includes
#include <vtkEventBroker.h>
#include <vtkMRMLSelectionDisplayNode.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLDisplayableManagerGroup.h>
#include <vtkMRMLScene.h>

// VTK includes
#include <vtkAbstractVolumeMapper.h>
#include <vtkActor2D.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkColorTransferFunction.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkLabelPlacementMapper.h>
#include <vtkMapper.h>
#include <vtkMapper2D.h>
#include <vtkOutlineGlowPass.h>
#include <vtkPoints.h>
#include <vtkPointSetToLabelHierarchy.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkRenderStepsPass.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>
#include <vtkVolumeProperty.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLFocusDisplayableManager);

class SoftFocusPipeline
{
public:
  vtkNew<vtkRenderer> SoftFocusRendererOutline;
  vtkNew<vtkRenderStepsPass> SoftFocusBasicPasses;
  vtkNew<vtkOutlineGlowPass> SoftFocusROIGlowPass;

  // Soft focus pipeline
  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> SoftFocusDisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> SoftFocusOriginalActors;
  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> SoftFocusOriginalToCopyActors;

  SoftFocusPipeline()
  {
    this->SoftFocusROIGlowPass->SetDelegatePass(this->SoftFocusBasicPasses);
    this->SoftFocusRendererOutline->UseFXAAOn();
    this->SoftFocusRendererOutline->UseShadowsOff();
    this->SoftFocusRendererOutline->UseDepthPeelingOff();
    this->SoftFocusRendererOutline->UseDepthPeelingForVolumesOff();
    this->SoftFocusRendererOutline->SetPass(this->SoftFocusROIGlowPass);
  }
};

class HardFocusPipeline
{
public:
  // Hard focus pipeline
  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> HardFocusDisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> HardFocusOriginalActors;
  vtkNew<vtkPolyData> HardFocusPolyData;
  vtkNew<vtkPolyDataMapper2D> HardFocusMapper;
  vtkNew<vtkActor2D> HardFocusActor;

  double Bounds_RAS[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };

  HardFocusPipeline()
  {
    double widthPx = 5.0;
    this->HardFocusMapper->SetInputData(this->HardFocusPolyData);
    this->HardFocusActor->SetMapper(this->HardFocusMapper);
    this->HardFocusActor->GetProperty()->SetLineWidth(widthPx);
  }

};

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

  std::map<vtkMRMLSelectionDisplayNode*, SoftFocusPipeline*> SoftFocusPipelines;
  std::map<vtkMRMLSelectionDisplayNode*, HardFocusPipeline*> HardFocusPipelines;

  vtkNew<vtkCallbackCommand> ObjectCallback;
  static void ObjectsCallback(vtkObject* caller, unsigned long eid,
    void* clientData, void* callData);

  void AddDisplayableNodeObservers(vtkMRMLDisplayableNode* displayNode);
  void AddDisplayableNodeObservers(std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>>& displayNode);
  void RemoveDisplayableNodeObservers(vtkMRMLDisplayableNode* displayNode);
  void RemoveDisplayableNodeObservers(std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>>& displayNode);
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::vtkInternal(vtkMRMLFocusDisplayableManager* external)
  : External(external)
{
  this->ObjectCallback->SetCallback(vtkMRMLFocusDisplayableManager::vtkInternal::ObjectsCallback);
  this->ObjectCallback->SetClientData(this->External);
}

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::~vtkInternal() = default;

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::AddDisplayableNodeObservers(vtkMRMLDisplayableNode* displayableNode)
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
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

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::AddDisplayableNodeObservers(std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>>& displayableNodes)
{
  for (vtkMRMLDisplayableNode* displayableNode : displayableNodes)
  {
    if (!displayableNode)
    {
      continue;
    }
    this->AddDisplayableNodeObservers(displayableNode);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::AddFocusedNodeObservers()
{
  for (auto pipelineIt = this->SoftFocusPipelines.begin();
    pipelineIt != this->SoftFocusPipelines.end(); ++pipelineIt)
  {
    SoftFocusPipeline* softFocusPipeline = pipelineIt->second;
    this->AddDisplayableNodeObservers(softFocusPipeline->SoftFocusDisplayableNodes);
    for (auto pipelineIt = this->HardFocusPipelines.begin(); pipelineIt != this->HardFocusPipelines.end(); ++pipelineIt)
    {
      this->AddDisplayableNodeObservers(pipelineIt->second->HardFocusDisplayableNodes);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::RemoveDisplayableNodeObservers(vtkMRMLDisplayableNode* displayableNode)
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
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

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::RemoveDisplayableNodeObservers(std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>>& displayNodes)
{
  for (auto pipelineIt = this->SoftFocusPipelines.begin();
    pipelineIt != this->SoftFocusPipelines.end(); ++pipelineIt)
  {
    for (vtkMRMLDisplayableNode* displayableNode : pipelineIt->second->SoftFocusDisplayableNodes)
    {
      if (!displayableNode)
      {
        continue;
      }
      this->RemoveDisplayableNodeObservers(displayableNode);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::RemoveFocusedNodeObservers()
{
  for (auto pipelineIt = this->SoftFocusPipelines.begin();
    pipelineIt != this->SoftFocusPipelines.end(); ++pipelineIt)
  {
    vtkEventBroker* broker = vtkEventBroker::GetInstance();
    for (auto pipelineIt = this->HardFocusPipelines.begin(); pipelineIt != this->HardFocusPipelines.end(); ++pipelineIt)
    {
      this->RemoveDisplayableNodeObservers(pipelineIt->second->HardFocusDisplayableNodes);
    }
    this->RemoveDisplayableNodeObservers(pipelineIt->second->SoftFocusDisplayableNodes);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::ObjectsCallback(vtkObject* caller, unsigned long eid,
  void* clientData, void* callData)
{
  vtkMRMLFocusDisplayableManager* external = reinterpret_cast<vtkMRMLFocusDisplayableManager*>(clientData);
  external->ProcesObjectsEvents(caller, eid, callData);
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
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  std::vector<vtkMRMLNode*> selectionNodes;
  this->GetMRMLScene()->GetNodesByClass("vtkMRMLSelectionNode", selectionNodes);

  for (vtkMRMLNode* node : selectionNodes)
  {
    vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(node);
    broker->RemoveObservations(selectionNode,
      vtkCommand::ModifiedEvent,
      this, this->GetMRMLNodesCallbackCommand());

    broker->AddObservation(selectionNode,
      vtkCommand::ModifiedEvent,
      this, this->GetMRMLNodesCallbackCommand());
    this->Internal->FocusWidget->SetSelectionNode(selectionNode);

    if (!broker->GetObservationExist(this->GetRenderer()->GetActiveCamera(), vtkCommand::ModifiedEvent,
      this, this->Internal->ObjectCallback))
    {
      broker->AddObservation(this->GetRenderer()->GetActiveCamera(), vtkCommand::ModifiedEvent,
        this, this->Internal->ObjectCallback);
    }

    if (!broker->GetObservationExist(this->GetRenderer()->GetRenderWindow(), vtkCommand::ModifiedEvent,
      this, this->Internal->ObjectCallback))
    {
      broker->AddObservation(this->GetRenderer()->GetRenderWindow(), vtkCommand::ModifiedEvent,
        this, this->Internal->ObjectCallback);
    }
  }

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

  if (vtkMRMLSelectionNode::SafeDownCast(caller) || vtkMRMLSelectionDisplayNode::SafeDownCast(caller))
  {
    this->UpdateFromMRML();
  }
  else if (vtkMRMLNode::SafeDownCast(caller))
  {
    this->UpdateFromDisplayableNode();
  }


  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::ProcesObjectsEvents(vtkObject* caller,
  unsigned long event,
  void* callData)
{
  if (vtkProp::SafeDownCast(caller))
  {
    this->UpdateActor(vtkProp::SafeDownCast(caller));
  }
  else if (vtkCoordinate::SafeDownCast(caller))
  {
    this->UpdateActors();
  }
  else if (vtkRenderWindow::SafeDownCast(caller) || vtkCamera::SafeDownCast(caller))
  {
    this->UpdateCornerROIPolyData();
  }

  this->Superclass::ProcessMRMLLogicsEvents(caller, event, callData);
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
void vtkMRMLFocusDisplayableManager::UpdateFromDisplayableNode()
{
  this->UpdateSoftFocusDisplayableNodes();
  this->UpdateOriginalSoftFocusActors();
  this->UpdateSoftFocus();


  this->UpdateHardFocusDisplayableNodes();
  this->UpdateOriginalHardFocusActors();
  this->UpdateHardFocus();

}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateFromMRML()
{
  this->Internal->RemoveFocusedNodeObservers();

  this->UpdateFromDisplayableNode();

  this->Internal->AddFocusedNodeObservers();

  this->SetUpdateFromMRMLRequested(false);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateHardFocusDisplayableNodes()
{
  vtkRenderer* renderer = this->GetRenderer();
  if (!renderer)
  {
    return;
  }

  for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
    pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
  {
    renderer->RemoveActor(pipelineIt->second->HardFocusActor);
    delete pipelineIt->second;
  }
  this->Internal->HardFocusPipelines.clear();

  std::vector<vtkMRMLNode*> selectionDisplayNodes;
  this->GetMRMLScene()->GetNodesByClass("vtkMRMLSelectionDisplayNode", selectionDisplayNodes);

  for (vtkMRMLNode* node : selectionDisplayNodes)
  {
    vtkMRMLSelectionDisplayNode* selectionDisplayNode = vtkMRMLSelectionDisplayNode::SafeDownCast(node);
    if (!selectionDisplayNode)
    {
      continue;
    }

    vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(selectionDisplayNode->GetDisplayableNode());

    this->Internal->HardFocusPipelines[selectionDisplayNode] = new HardFocusPipeline();

    for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
      pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
    {
      pipelineIt->second->HardFocusDisplayableNodes.clear();

      vtkMRMLDisplayableNode* focusedNode = vtkMRMLDisplayableNode::SafeDownCast(selectionNode->GetFocusNode());
      if (focusedNode)
      {
        pipelineIt->second->HardFocusDisplayableNodes.push_back(focusedNode);
      }
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateSoftFocusDisplayableNodes()
{
  vtkRenderer* renderer = this->GetRenderer();

  for (auto pipelineIt = this->Internal->SoftFocusPipelines.begin();
    pipelineIt != this->Internal->SoftFocusPipelines.end(); ++pipelineIt)
  {
    for (auto actorIt = pipelineIt->second->SoftFocusOriginalToCopyActors.begin();
      actorIt != pipelineIt->second->SoftFocusOriginalToCopyActors.end(); ++actorIt)
    {
      pipelineIt->second->SoftFocusRendererOutline->RemoveViewProp(actorIt->second);
    }
    renderer->GetRenderWindow()->RemoveRenderer(pipelineIt->second->SoftFocusRendererOutline);

    delete pipelineIt->second;
  }
  this->Internal->SoftFocusPipelines.clear();

  std::vector<vtkMRMLNode*> selectionDisplayNodes;
  this->GetMRMLScene()->GetNodesByClass("vtkMRMLSelectionDisplayNode", selectionDisplayNodes);

  for (vtkMRMLNode* node : selectionDisplayNodes)
  {
    vtkMRMLSelectionDisplayNode* selectionDisplayNode = vtkMRMLSelectionDisplayNode::SafeDownCast(node);
    if (!selectionDisplayNode)
    {
      continue;
    }
    this->Internal->SoftFocusPipelines[selectionDisplayNode] = new SoftFocusPipeline();
  }

  for (auto pipelineIt = this->Internal->SoftFocusPipelines.begin();
    pipelineIt != this->Internal->SoftFocusPipelines.end(); ++pipelineIt)
  {
    SoftFocusPipeline* softFocusPipeline = pipelineIt->second;
    softFocusPipeline->SoftFocusDisplayableNodes.clear();

    vtkMRMLSelectionDisplayNode* displayNode = pipelineIt->first;
    vtkMRMLSelectionNode* selectionnode = vtkMRMLSelectionNode::SafeDownCast(displayNode->GetDisplayableNode());

    int numberOfSoftFocusNodes = selectionnode ?
      selectionnode->GetNumberOfNodeReferences(selectionnode->GetSoftFocusNodeReferenceRole()) : 0;
    std::vector<vtkMRMLDisplayableNode*> displayableNodes;
    for (int i = 0; i < numberOfSoftFocusNodes; ++i)
    {
      vtkMRMLDisplayableNode* softFocusedNode =
        vtkMRMLDisplayableNode::SafeDownCast(selectionnode->GetNthSoftFocusNode(i));
      if (!softFocusedNode)
      {
        continue;
      }
      softFocusPipeline->SoftFocusDisplayableNodes.push_back(softFocusedNode);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateOriginalHardFocusActors()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
    pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
  {
    for (vtkProp* oldActor : pipelineIt->second->HardFocusOriginalActors)
    {
      if (!oldActor)
      {
        continue;
      }
      broker->RemoveObservations(oldActor, vtkCommand::ModifiedEvent,
        this, this->Internal->ObjectCallback);

      vtkActor2D* oldActor2D = vtkActor2D::SafeDownCast(oldActor);
      if (oldActor2D)
      {
        // Need to update copied actors when the position of the 2D actor changes
        broker->RemoveObservations(oldActor2D->GetPositionCoordinate(),
          vtkCommand::ModifiedEvent,
          this, this->Internal->ObjectCallback);
      }
    }
    pipelineIt->second->HardFocusOriginalActors.clear();
  }

  std::vector<vtkMRMLDisplayNode*> displayNodes;
  for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
    pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
  {
    vtkMRMLSelectionDisplayNode* selectionDisplayNode = pipelineIt->first;
    vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(selectionDisplayNode->GetDisplayableNode());

    for (vtkMRMLDisplayableNode* displayableNode : pipelineIt->second->HardFocusDisplayableNodes)
    {
      for (int i = 0; i < displayableNode->GetNumberOfDisplayNodes(); ++i)
      {
        vtkMRMLDisplayNode* displayNode = displayableNode->GetNthDisplayNode(i);
        if (!displayNode)
        {
          continue;
        }
        displayNodes.push_back(displayNode);
      }
    }

    vtkNew<vtkPropCollection> focusNodeActors;
    vtkMRMLDisplayableManagerGroup* group = this->GetMRMLDisplayableManagerGroup();
    for (vtkMRMLDisplayNode* displayNode : displayNodes)
    {
      for (int i = 0; i < group->GetDisplayableManagerCount(); ++i)
      {
        vtkMRMLAbstractDisplayableManager* displayableManager = group->GetNthDisplayableManager(i);
        if (displayableManager == this)
        {
          // Ignore focus display manager.
          continue;
        }
        displayableManager->GetActorsByDisplayNode(focusNodeActors, displayNode,
          selectionNode->GetFocusedComponentType(), selectionNode->GetFocusedComponentIndex());
      }
    }

    vtkSmartPointer<vtkProp> prop = nullptr;
    vtkCollectionSimpleIterator it = nullptr;
    for (focusNodeActors->InitTraversal(it); prop = focusNodeActors->GetNextProp(it);)
    {
      if (!prop->GetVisibility())
      {
        // Ignore actors that are not visible.
        continue;
      }

      pipelineIt->second->HardFocusOriginalActors.push_back(prop);

      broker->AddObservation(prop,
        vtkCommand::ModifiedEvent,
        this, this->Internal->ObjectCallback);

      vtkActor2D* actor2D = vtkActor2D::SafeDownCast(prop);
      if (actor2D)
      {
        broker->AddObservation(actor2D->GetPositionCoordinate(),
          vtkCommand::ModifiedEvent,
          this, this->Internal->ObjectCallback);
      }
    }
  }
}


//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateOriginalSoftFocusActors()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (auto pipelineIt = this->Internal->SoftFocusPipelines.begin();
    pipelineIt != this->Internal->SoftFocusPipelines.end(); ++pipelineIt)
  {
    SoftFocusPipeline* softFocusPipeline = pipelineIt->second;

    for (vtkProp* oldActor : softFocusPipeline->SoftFocusOriginalActors)
    {
      if (!oldActor)
      {
        continue;
      }
      broker->RemoveObservations(oldActor, vtkCommand::ModifiedEvent,
        this, this->Internal->ObjectCallback);

      vtkActor2D* oldActor2D = vtkActor2D::SafeDownCast(oldActor);
      if (oldActor2D)
      {
        // Need to update copied actors when the position of the 2D actor changes
        broker->RemoveObservations(oldActor2D->GetPositionCoordinate(),
          vtkCommand::ModifiedEvent,
          this, this->Internal->ObjectCallback);
      }
    }

    softFocusPipeline->SoftFocusOriginalActors.clear();

    struct A
    {
      vtkMRMLDisplayableNode* DisplayableNode{ nullptr };
      int ComponentType{ -1 };
      int ComponentIndex{ -1 };
    };
    std::map<vtkMRMLDisplayNode*, A> info;

    std::vector<vtkMRMLDisplayNode*> displayNodes;
    for (vtkMRMLDisplayableNode* displayableNode : softFocusPipeline->SoftFocusDisplayableNodes)
    {
      for (int i = 0; i < displayableNode->GetNumberOfDisplayNodes(); ++i)
      {
        vtkMRMLDisplayNode* displayNode = displayableNode->GetNthDisplayNode(i);
        if (!displayNode)
        {
          continue;
        }
        displayNodes.push_back(displayNode);

        A newInfo;
        newInfo.DisplayableNode = displayableNode;
        this->GetSelectionNode()->GetSoftFocusComponent(displayableNode->GetID(),
          newInfo.ComponentType, newInfo.ComponentIndex);
        info[displayNode] = newInfo;
      }
    }

    vtkNew<vtkPropCollection> focusNodeActors;
    vtkMRMLDisplayableManagerGroup* group = this->GetMRMLDisplayableManagerGroup();
    for (vtkMRMLDisplayNode* displayNode : displayNodes)
    {
      for (int i = 0; i < group->GetDisplayableManagerCount(); ++i)
      {
        vtkMRMLAbstractDisplayableManager* displayableManager = group->GetNthDisplayableManager(i);
        if (displayableManager == this)
        {
          // Ignore focus display manager.
          continue;
        }

        A b = info[displayNode];

        displayableManager->GetActorsByDisplayNode(focusNodeActors, displayNode,
          b.ComponentType, b.ComponentIndex);
      }
    }

    vtkSmartPointer<vtkProp> prop = nullptr;
    vtkCollectionSimpleIterator it = nullptr;
    for (focusNodeActors->InitTraversal(it); prop = focusNodeActors->GetNextProp(it);)
    {
      if (!prop->GetVisibility())
      {
        // Ignore actors that are not visible.
        continue;
      }

      softFocusPipeline->SoftFocusOriginalActors.push_back(prop);

      broker->AddObservation(prop,
        vtkCommand::ModifiedEvent,
        this, this->Internal->ObjectCallback);

      vtkActor2D* actor2D = vtkActor2D::SafeDownCast(prop);
      if (actor2D)
      {
        broker->AddObservation(actor2D->GetPositionCoordinate(),
          vtkCommand::ModifiedEvent,
          this, this->Internal->ObjectCallback);
      }
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateSoftFocus()
{
  for (auto pipelineIt = this->Internal->SoftFocusPipelines.begin();
    pipelineIt != this->Internal->SoftFocusPipelines.end(); ++pipelineIt)
  {
    pipelineIt->second->SoftFocusRendererOutline->RemoveAllViewProps();

    vtkMRMLSelectionDisplayNode* displayNode = pipelineIt->first;
    vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(displayNode->GetDisplayableNode());
    /*
    int numberOfSoftFocusNodes = selectionNode ?
      selectionNode->GetNumberOfNodeReferences(selectionNode->GetSoftFocusNodeReferenceRole()) : 0;
    */

    vtkRenderer* renderer = this->GetRenderer();
    if (/*numberOfSoftFocusNodes <= 0 || */!renderer)
    {
      return;
    }

    int RENDERER_LAYER = 1;
    if (renderer->GetRenderWindow()->GetNumberOfLayers() < RENDERER_LAYER + 1)
    {
      renderer->GetRenderWindow()->SetNumberOfLayers(RENDERER_LAYER + 1);
    }

    pipelineIt->second->SoftFocusROIGlowPass->SetOutlineIntensity(1000.0);
    //this->Internal->SoftFocusROIGlowPass->SetOutlineIntensity(selectionNode->GetFocusedHighlightStrength());
    pipelineIt->second->SoftFocusRendererOutline->SetLayer(RENDERER_LAYER);

    std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> newOriginalToCopyActors;

    vtkEventBroker* broker = vtkEventBroker::GetInstance();

    vtkSmartPointer<vtkProp> prop = nullptr;
    vtkCollectionSimpleIterator it = nullptr;
    for (vtkProp* originalProp : pipelineIt->second->SoftFocusOriginalActors)
    {
      if (!originalProp->GetVisibility())
      {
        // Ignore actors that are not visible.
        continue;
      }

      vtkSmartPointer<vtkProp> newProp = pipelineIt->second->SoftFocusOriginalToCopyActors[originalProp];
      if (!newProp)
      {
        newProp = vtkSmartPointer<vtkProp>::Take(originalProp->NewInstance());
        newProp->SetPickable(false);
      }

      newOriginalToCopyActors[originalProp] = newProp;
      pipelineIt->second->SoftFocusRendererOutline->AddViewProp(originalProp);
    }
    pipelineIt->second->SoftFocusOriginalToCopyActors = newOriginalToCopyActors;

    this->UpdateActors();

    pipelineIt->second->SoftFocusRendererOutline->SetActiveCamera(renderer->GetActiveCamera());
    if (!renderer->GetRenderWindow()->HasRenderer(pipelineIt->second->SoftFocusRendererOutline))
    {
      renderer->GetRenderWindow()->AddRenderer(pipelineIt->second->SoftFocusRendererOutline);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateHardFocus()
{
  vtkRenderer* renderer = this->GetRenderer();
  if (!renderer)
  {
    return;
  }

  this->UpdateActorRASBounds();
  this->UpdateCornerROIPolyData();

  for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
    pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
  {
    vtkActor2D* actor = pipelineIt->second->HardFocusActor;
    if (!renderer->HasViewProp(actor))
    {
      renderer->AddActor(actor);
    }

    actor->GetProperty()->SetColor(pipelineIt->first->GetHighlightColor());
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActorRASBounds()
{
  for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
    pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
  {
    HardFocusPipeline* pipeline = pipelineIt->second;
    pipeline->Bounds_RAS[0] = VTK_DOUBLE_MAX;
    pipeline->Bounds_RAS[1] = VTK_DOUBLE_MIN;
    pipeline->Bounds_RAS[2] = VTK_DOUBLE_MAX;
    pipeline->Bounds_RAS[3] = VTK_DOUBLE_MIN;
    pipeline->Bounds_RAS[4] = VTK_DOUBLE_MAX;
    pipeline->Bounds_RAS[5] = VTK_DOUBLE_MIN;

    for (vtkProp* originalProp : pipelineIt->second->HardFocusOriginalActors)
    {
      double* currentBounds = originalProp->GetBounds();
      if (currentBounds)
      {
        for (int i = 0; i < 6; i += 2)
        {
          pipeline->Bounds_RAS[i] = std::min(pipeline->Bounds_RAS[i], currentBounds[i]);
          pipeline->Bounds_RAS[i + 1] = std::max(pipeline->Bounds_RAS[i + 1], currentBounds[i + 1]);
        }
      }
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateCornerROIPolyData()
{
  for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
    pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
  {
    vtkMRMLSelectionDisplayNode* displayNode = pipelineIt->first;
    vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(displayNode->GetDisplayableNode());
    vtkMRMLNode* focusedNode = selectionNode ? selectionNode->GetFocusNode() : nullptr;

    for (auto pipelineIt = this->Internal->HardFocusPipelines.begin();
      pipelineIt != this->Internal->HardFocusPipelines.end(); ++pipelineIt)
    {
      HardFocusPipeline* pipeline = pipelineIt->second;

      bool boundsValid = true;
      if (pipeline->Bounds_RAS[0] > pipeline->Bounds_RAS[1]
        || pipeline->Bounds_RAS[2] > pipeline->Bounds_RAS[3]
        || pipeline->Bounds_RAS[4] > pipeline->Bounds_RAS[5])
      {
        boundsValid = false;
      }

      vtkRenderer* renderer = this->GetRenderer();
      if (!renderer || !focusedNode || !boundsValid)
      {
        pipelineIt->second->HardFocusPolyData->Initialize();
        continue;
      }

      vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(this->GetMRMLDisplayableNode());
      if (sliceNode)
      {
        // TODO: Hard focus is currently only visualized in 3D.
        pipelineIt->second->HardFocusPolyData->Initialize();
        continue;
      }

      double displayBounds[4] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
      for (int k = 4; k < 6; ++k)
      {
        for (int j = 2; j < 4; ++j)
        {
          for (int i = 0; i < 2; ++i)
          {
            double point_RAS[4] = {
              pipeline->Bounds_RAS[i],
              pipeline->Bounds_RAS[j],
              pipeline->Bounds_RAS[k],
              1.0 };
            renderer->SetWorldPoint(point_RAS);
            renderer->WorldToDisplay();
            double* displayPoint = renderer->GetDisplayPoint();

            displayBounds[0] = std::min(displayBounds[0], displayPoint[0]);
            displayBounds[1] = std::max(displayBounds[1], displayPoint[0]);
            displayBounds[2] = std::min(displayBounds[2], displayPoint[1]);
            displayBounds[3] = std::max(displayBounds[3], displayPoint[1]);
          }
        }
      }

      for (int i = 0; i < 4; ++i)
      {
        displayBounds[i] = std::max(0.0, displayBounds[i]);
      }

      int* displaySize = renderer->GetSize();
      displayBounds[0] = std::min(displayBounds[0], static_cast<double>(displaySize[0]));
      displayBounds[1] = std::min(displayBounds[1], static_cast<double>(displaySize[0]));
      displayBounds[2] = std::min(displayBounds[2], static_cast<double>(displaySize[1]));
      displayBounds[3] = std::min(displayBounds[3], static_cast<double>(displaySize[1]));

      double lenPx = 10.0;
      vtkPoints* outlinePoints = pipelineIt->second->HardFocusPolyData->GetPoints();
      if (!outlinePoints)
      {
        pipelineIt->second->HardFocusPolyData->SetPoints(vtkNew<vtkPoints>());
        outlinePoints = pipelineIt->second->HardFocusPolyData->GetPoints();
      }

      if (outlinePoints->GetNumberOfPoints() != 12)
      {
        outlinePoints->SetNumberOfPoints(12);
      }

      int pointIndex = 0;
      outlinePoints->SetPoint(pointIndex++, displayBounds[0] + lenPx, displayBounds[2], 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[0], displayBounds[2], 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[0], displayBounds[2] + lenPx, 0.0);

      outlinePoints->SetPoint(pointIndex++, displayBounds[0], displayBounds[3] - lenPx, 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[0], displayBounds[3], 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[0] + lenPx, displayBounds[3], 0.0);

      outlinePoints->SetPoint(pointIndex++, displayBounds[1] - lenPx, displayBounds[3], 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[1], displayBounds[3], 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[1], displayBounds[3] - lenPx, 0.0);

      outlinePoints->SetPoint(pointIndex++, displayBounds[1], displayBounds[2] + lenPx, 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[1], displayBounds[2], 0.0);
      outlinePoints->SetPoint(pointIndex++, displayBounds[1] - lenPx, displayBounds[2], 0.0);

      vtkSmartPointer<vtkCellArray> lines = pipelineIt->second->HardFocusPolyData->GetLines();
      if (!lines || lines->GetNumberOfCells() == 0)
      {
        lines = vtkSmartPointer<vtkCellArray>::New();

        pointIndex = 0;
        for (int lineIndex = 0; lineIndex < 4; ++lineIndex)
        {
          int point0 = pointIndex++;
          int point1 = pointIndex++;
          int point2 = pointIndex++;

          vtkNew<vtkIdList> cornerA;
          cornerA->InsertNextId(point0);
          cornerA->InsertNextId(point1);
          lines->InsertNextCell(cornerA);

          vtkNew<vtkIdList> cornerB;
          cornerB->InsertNextId(point2);
          cornerB->InsertNextId(point1);
          lines->InsertNextCell(cornerB);
        }

        pipelineIt->second->HardFocusPolyData->SetLines(lines);
      }

      outlinePoints->Modified();
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActors()
{
  for (auto pipelineIt = this->Internal->SoftFocusPipelines.begin();
    pipelineIt != this->Internal->SoftFocusPipelines.end(); ++pipelineIt)
  {
    SoftFocusPipeline* softFocusPipeline = pipelineIt->second;
    for (vtkProp* originalProp : softFocusPipeline->SoftFocusOriginalActors)
    {
      if (!originalProp)
      {
        continue;
      }
      this->UpdateActor(originalProp);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActor(vtkProp* originalProp)
{
  for (auto pipelineIt = this->Internal->SoftFocusPipelines.begin();
    pipelineIt != this->Internal->SoftFocusPipelines.end(); ++pipelineIt)
  {
    SoftFocusPipeline* softFocusPipeline = pipelineIt->second;

    vtkProp* copyProp = softFocusPipeline->SoftFocusOriginalToCopyActors[originalProp];
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
      copyProperty->SetColor(1.0, 0.0, 0.0);
      copyProperty->SetOpacity(1.0);
      copyActor->SetProperty(copyProperty);
    }

    vtkVolume* copyVolume = vtkVolume::SafeDownCast(copyProp);
    if (copyVolume)
    {
      vtkNew<vtkColorTransferFunction> colorTransferFunction;
      colorTransferFunction->AddRGBPoint(0, 1.0, 1.0, 1.0);

      vtkSmartPointer<vtkVolumeProperty> newProperty = vtkSmartPointer<vtkVolumeProperty>::Take(copyVolume->GetProperty()->NewInstance());
      newProperty->DeepCopy(copyVolume->GetProperty());
      newProperty->SetDiffuse(0.0);
      newProperty->SetAmbient(1.0);
      newProperty->ShadeOff();
      newProperty->SetColor(colorTransferFunction);
      copyVolume->SetProperty(newProperty);
    }

    vtkActor2D* newActor2D = vtkActor2D::SafeDownCast(copyProp);
    if (newActor2D)
    {
      vtkSmartPointer<vtkProperty2D> newProperty2D = vtkSmartPointer<vtkProperty2D>::Take(newActor2D->GetProperty()->NewInstance());
      newProperty2D->DeepCopy(newActor2D->GetProperty());
      newProperty2D->SetColor(1.0, 1.0, 1.0);
      newActor2D->SetProperty(newProperty2D);
    }

    vtkLabelPlacementMapper* oldLabelMapper = newActor2D ? vtkLabelPlacementMapper::SafeDownCast(newActor2D->GetMapper()) : nullptr;
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
        textProperty->SetOpacity(1.0);
        newPointSetInput->SetTextProperty(textProperty);

        newLabelMapper->SetInputConnection(newPointSetInput->GetOutputPort());

        newActor2D->SetMapper(newLabelMapper);
      }
    }

    vtkTextActor* textActor = vtkTextActor::SafeDownCast(copyProp);
    if (textActor)
    {
      // TODO: Outline is not large enough if background is fully transparent.
      vtkSmartPointer<vtkTextProperty> textProperty = vtkSmartPointer<vtkTextProperty>::Take(textActor->GetTextProperty()->NewInstance());
      textProperty->ShallowCopy(textActor->GetTextProperty());
      textProperty->SetBackgroundRGBA(1.0, 1.0, 1.0, 1.0);
      textActor->SetTextProperty(textProperty);
    }
  }
}
