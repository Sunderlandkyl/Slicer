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
  vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;

  // Soft focus pipeline
  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> SoftFocusDisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> SoftFocusOriginalActors;
  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> SoftFocusOriginalToCopyActors;
  vtkNew<vtkRenderer> SoftFocusRendererOutline;
  vtkNew<vtkRenderStepsPass> SoftFocusBasicPasses;
  vtkNew<vtkOutlineGlowPass> SoftFocusROIGlowPass;

  // Hard focus pipeline
  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> HardFocusDisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> HardFocusOriginalActors;
  vtkNew<vtkPolyData> HardFocusPolyData;
  vtkNew<vtkPolyDataMapper2D> HardFocusMapper;
  vtkNew<vtkActor2D> HardFocusActor;

  vtkNew<vtkCallbackCommand> ObjectCallback;
  static void ObjectsCallback(vtkObject* caller, unsigned long eid,
    void* clientData, void* callData);

  double Bounds_RAS[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };

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
  this->SoftFocusROIGlowPass->SetDelegatePass(this->SoftFocusBasicPasses);
  this->SoftFocusRendererOutline->UseFXAAOn();
  this->SoftFocusRendererOutline->UseShadowsOff();
  this->SoftFocusRendererOutline->UseDepthPeelingOff();
  this->SoftFocusRendererOutline->UseDepthPeelingForVolumesOff();
  this->SoftFocusRendererOutline->SetPass(this->SoftFocusROIGlowPass);

  double widthPx = 5.0;
  this->HardFocusMapper->SetInputData(this->HardFocusPolyData);
  this->HardFocusActor->SetMapper(this->HardFocusMapper);
  this->HardFocusActor->GetProperty()->SetLineWidth(widthPx);

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
  this->AddDisplayableNodeObservers(this->SoftFocusDisplayableNodes);
  this->AddDisplayableNodeObservers(this->HardFocusDisplayableNodes);
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
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
  for (vtkMRMLDisplayableNode* displayableNode : this->SoftFocusDisplayableNodes)
  {
    if (!displayableNode)
    {
      continue;
    }
    this->RemoveDisplayableNodeObservers(displayableNode);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::vtkInternal::RemoveFocusedNodeObservers()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  this->RemoveDisplayableNodeObservers(this->SoftFocusDisplayableNodes);
  this->RemoveDisplayableNodeObservers(this->HardFocusDisplayableNodes);
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
vtkMRMLNode* vtkMRMLFocusDisplayableManager::GetFocusNode()
{
  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
  const char* focusNodeID = selectionNode ? selectionNode->GetFocusNodeID() : nullptr;
  vtkMRMLNode* focusedNode =
    vtkMRMLNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(focusNodeID));
  return focusedNode;
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
  if (this->Internal->SelectionNode)
  {
    broker->RemoveObservations(this->Internal->SelectionNode,
      vtkCommand::ModifiedEvent,
      this, this->GetMRMLNodesCallbackCommand());
  }

  this->Internal->SelectionNode = this->GetSelectionNode();
  broker->AddObservation(this->Internal->SelectionNode,
    vtkCommand::ModifiedEvent,
    this, this->GetMRMLNodesCallbackCommand());
  this->Internal->FocusWidget->SetSelectionNode(this->Internal->SelectionNode);

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

  if (vtkMRMLSelectionNode::SafeDownCast(caller))
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

  /*
  this->UpdateHardFocusDisplayableNodes();
  this->UpdateOriginalHardFocusActors();
  this->UpdateHardFocus();
  */
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
  this->Internal->HardFocusDisplayableNodes.clear();

  vtkMRMLDisplayableNode* focusedNode = vtkMRMLDisplayableNode::SafeDownCast(this->GetFocusNode());
  if (focusedNode)
  {
    this->Internal->HardFocusDisplayableNodes.push_back(focusedNode);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateSoftFocusDisplayableNodes()
{
  this->Internal->SoftFocusDisplayableNodes.clear();

  int numberOfSoftFocusNodes = this->Internal->SelectionNode ?
    this->Internal->SelectionNode->GetNumberOfNodeReferences(this->Internal->SelectionNode->GetSoftFocusNodeReferenceRole()) : 0;
  std::vector<vtkMRMLDisplayableNode*> displayableNodes;
  for (int i = 0; i < numberOfSoftFocusNodes; ++i)
  {
    vtkMRMLDisplayableNode* softFocusedNode =
      vtkMRMLDisplayableNode::SafeDownCast(this->Internal->SelectionNode->GetNthSoftFocusNode(i));
    if (!softFocusedNode)
    {
      continue;
    }
    this->Internal->SoftFocusDisplayableNodes.push_back(softFocusedNode);
  }

  vtkMRMLDisplayableNode* focusedNode = vtkMRMLDisplayableNode::SafeDownCast(this->GetFocusNode());
  if (focusedNode)
  {
    this->Internal->SoftFocusDisplayableNodes.push_back(focusedNode);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateOriginalHardFocusActors()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (vtkProp* oldActor : this->Internal->HardFocusOriginalActors)
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

  this->Internal->HardFocusOriginalActors.clear();

  std::vector<vtkMRMLDisplayNode*> displayNodes;
  for (vtkMRMLDisplayableNode* displayableNode : this->Internal->HardFocusDisplayableNodes)
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
        this->Internal->SelectionNode->GetFocusedComponentType(), this->Internal->SelectionNode->GetFocusedComponentIndex());
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

    this->Internal->HardFocusOriginalActors.push_back(prop);

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


//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateOriginalSoftFocusActors()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (vtkProp* oldActor : this->Internal->SoftFocusOriginalActors)
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

  this->Internal->SoftFocusOriginalActors.clear();

  struct A
  {
    vtkMRMLDisplayableNode* DisplayableNode{nullptr};
    int ComponentType{-1};
    int ComponentIndex{-1};
  };
  std::map<vtkMRMLDisplayNode*, A> info;

  std::vector<vtkMRMLDisplayNode*> displayNodes;
  for (vtkMRMLDisplayableNode* displayableNode : this->Internal->SoftFocusDisplayableNodes)
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

    this->Internal->SoftFocusOriginalActors.push_back(prop);

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

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateSoftFocus()
{
  this->Internal->SoftFocusRendererOutline->RemoveAllViewProps();

  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
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

  this->Internal->SoftFocusROIGlowPass->SetOutlineIntensity(selectionNode->GetFocusedHighlightStrength());
  this->Internal->SoftFocusRendererOutline->SetLayer(RENDERER_LAYER);

  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> newOriginalToCopyActors;

  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  vtkSmartPointer<vtkProp> prop = nullptr;
  vtkCollectionSimpleIterator it = nullptr;
  for (vtkProp* originalProp : this->Internal->SoftFocusOriginalActors)
  {
    if (!originalProp->GetVisibility())
    {
      // Ignore actors that are not visible.
      continue;
    }

    vtkSmartPointer<vtkProp> newProp = this->Internal->SoftFocusOriginalToCopyActors[originalProp];
    if (!newProp)
    {
      newProp = vtkSmartPointer<vtkProp>::Take(originalProp->NewInstance());
      newProp->SetPickable(false);
    }

    newOriginalToCopyActors[originalProp] = newProp;
    this->Internal->SoftFocusRendererOutline->AddViewProp(originalProp);
  }
  this->Internal->SoftFocusOriginalToCopyActors = newOriginalToCopyActors;

  this->UpdateActors();

  this->Internal->SoftFocusRendererOutline->SetActiveCamera(renderer->GetActiveCamera());
  if (!renderer->GetRenderWindow()->HasRenderer(this->Internal->SoftFocusRendererOutline))
  {
    renderer->GetRenderWindow()->AddRenderer(this->Internal->SoftFocusRendererOutline);
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

  if (!renderer->HasViewProp(this->Internal->HardFocusActor))
  {
    renderer->AddActor(this->Internal->HardFocusActor);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActorRASBounds()
{
  this->Internal->Bounds_RAS[0] = VTK_DOUBLE_MAX;
  this->Internal->Bounds_RAS[1] = VTK_DOUBLE_MIN;
  this->Internal->Bounds_RAS[2] = VTK_DOUBLE_MAX;
  this->Internal->Bounds_RAS[3] = VTK_DOUBLE_MIN;
  this->Internal->Bounds_RAS[4] = VTK_DOUBLE_MAX;
  this->Internal->Bounds_RAS[5] = VTK_DOUBLE_MIN;

  for (vtkProp* originalProp : this->Internal->HardFocusOriginalActors)
  {
    double* currentBounds = originalProp->GetBounds();
    if (currentBounds)
    {
      for (int i = 0; i < 6; i += 2)
      {
        this->Internal->Bounds_RAS[i] = std::min(this->Internal->Bounds_RAS[i], currentBounds[i]);
        this->Internal->Bounds_RAS[i + 1] = std::max(this->Internal->Bounds_RAS[i + 1], currentBounds[i + 1]);
      }
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateCornerROIPolyData()
{
  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
  vtkMRMLNode* focusedNode = selectionNode ? selectionNode->GetFocusNode() : nullptr;

  bool boundsValid = true;
  if (this->Internal->Bounds_RAS[0] > this->Internal->Bounds_RAS[1]
    || this->Internal->Bounds_RAS[2] > this->Internal->Bounds_RAS[3]
    || this->Internal->Bounds_RAS[4] > this->Internal->Bounds_RAS[5])
  {
    boundsValid = false;
  }

  vtkRenderer* renderer = this->GetRenderer();
  if (!renderer || !focusedNode || !boundsValid)
  {
    this->Internal->HardFocusPolyData->Initialize();
    return;
  }

  vtkMRMLSliceNode* sliceNode = vtkMRMLSliceNode::SafeDownCast(this->GetMRMLDisplayableNode());
  if (sliceNode)
  {
    // TODO: Hard focus is currently only visualized in 3D.
    this->Internal->HardFocusPolyData->Initialize();
    return;
  }

  double displayBounds[4] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
  for (int k = 4; k < 6; ++k)
  {
    for (int j = 2; j < 4; ++j)
    {
      for (int i = 0; i < 2; ++i)
      {
        double point_RAS[4] = { this->Internal->Bounds_RAS[i], this->Internal->Bounds_RAS[j], this->Internal->Bounds_RAS[k], 1.0};
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
  vtkPoints* outlinePoints = this->Internal->HardFocusPolyData->GetPoints();
  if (!outlinePoints)
  {
    this->Internal->HardFocusPolyData->SetPoints(vtkNew<vtkPoints>());
    outlinePoints = this->Internal->HardFocusPolyData->GetPoints();
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

  vtkSmartPointer<vtkCellArray> lines = this->Internal->HardFocusPolyData->GetLines();
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

    this->Internal->HardFocusPolyData->SetLines(lines);
  }

  outlinePoints->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActors()
{
  for (vtkProp* originalProp : this->Internal->SoftFocusOriginalActors)
  {
    if (!originalProp)
    {
      continue;
    }
    this->UpdateActor(originalProp);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActor(vtkProp* originalProp)
{
  vtkProp* copyProp = this->Internal->SoftFocusOriginalToCopyActors[originalProp];
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
