/*=========================================================================

  Copyright 2005 Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

==========================================================================*/

// MRMLDisplayableManager includes
#include "vtkMRMLFocusDisplayableManager.h"
#include "vtkMRMLApplicationLogic.h"

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
#include <vtkMapper.h>
#include <vtkMapper2D.h>
#include <vtkOutlineGlowPass.h>
#include <vtkPolyDataMapper.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkRenderStepsPass.h>
#include <vtkRenderer.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
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

  vtkNew<vtkRenderer> RendererOutline;
  vtkNew<vtkRenderStepsPass> BasicPasses;
  vtkNew<vtkOutlineGlowPass> ROIGlowPass;

  vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;
  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> DisplayableNodes;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::vtkInternal(vtkMRMLFocusDisplayableManager* external)
  : External(external)
{
  this->ROIGlowPass->SetDelegatePass(this->BasicPasses);
  this->RendererOutline->UseFXAAOn();
}

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::~vtkInternal() = default;

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

  if (this->GetRenderer() &&
    this->GetRenderer()->GetRenderWindow() &&
    this->GetRenderer()->GetRenderWindow()->CheckInRenderStatus())
    {
    vtkDebugMacro("skipping ProcessMRMLNodesEvents during render");
    return;
    }

  this->UpdateFromMRML();
  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateFromMRML()
{
  this->Internal->RendererOutline->RemoveAllViewProps();

  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
  const char* focusNodeID = selectionNode ? selectionNode->GetFocusNodeID() : nullptr;
  vtkMRMLDisplayableNode* focusedNode = nullptr;
  if (focusNodeID)
    {
    focusedNode = vtkMRMLDisplayableNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(focusNodeID));
    }

  vtkRenderer* renderer = this->GetRenderer();
  if (!selectionNode || !focusedNode || !renderer || focusedNode->GetNumberOfDisplayNodes() == 0)
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
  this->Internal->RendererOutline->SetPass(this->Internal->ROIGlowPass);

  this->Internal->RemoveFocusedNodeObservers();
  this->Internal->DisplayableNodes.clear();
  this->Internal->DisplayableNodes.push_back(focusedNode);
  this->Internal->AddFocusedNodeObservers();

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
    this->SetUpdateFromMRMLRequested(false);
    this->RequestRender();
    return;
    }

  for (int i = 0; i < focusNodeActors->GetNumberOfItems(); ++i)
    {
    vtkProp* prop = vtkProp::SafeDownCast(focusNodeActors->GetItemAsObject(i));
    if (!prop)
      {
      continue;
      }

    vtkSmartPointer<vtkProp> newProp = vtkSmartPointer<vtkProp>::Take(prop->NewInstance());
    newProp->ShallowCopy(prop);

    vtkActor* newActor = vtkActor::SafeDownCast(newProp);
    if (newActor)
      {
      // Make the actor flat. This generates a better outline.
      vtkSmartPointer<vtkProperty> newProperty = vtkSmartPointer<vtkProperty>::Take(newActor->GetProperty()->NewInstance());
      newProperty->DeepCopy(newActor->GetProperty());
      newProperty->SetLighting(false);
      newProperty->SetColor(1.0, 1.0, 1.0);
      newActor->SetProperty(newProperty);
      }

    vtkVolume* newVolume = vtkVolume::SafeDownCast(newProp);
    if (newVolume)
      {
      vtkNew<vtkColorTransferFunction> colorTransferFunction;
      colorTransferFunction->AddRGBPoint(0, 1.0, 1.0, 1.0);

      vtkSmartPointer<vtkVolumeProperty> newProperty = vtkSmartPointer<vtkVolumeProperty>::Take(newVolume->GetProperty()->NewInstance());;
      newProperty->DeepCopy(newVolume->GetProperty());
      newProperty->SetDiffuse(0.0);
      newProperty->SetAmbient(1.0);
      newProperty->ShadeOff();
      newProperty->SetColor(colorTransferFunction);

      newVolume->SetProperty(newProperty);
      }

    vtkActor2D* newActor2D = vtkActor2D::SafeDownCast(newProp);
    if (newActor2D)
      {
      vtkSmartPointer<vtkProperty2D> newProperty2D = vtkSmartPointer<vtkProperty2D>::Take(newActor2D->GetProperty()->NewInstance());
      newProperty2D->DeepCopy(newActor2D->GetProperty());
      newProperty2D->SetColor(1.0, 1.0, 1.0);
      newActor2D->SetProperty(newProperty2D);
      }

    this->Internal->RendererOutline->AddViewProp(newProp);
    }

  this->Internal->RendererOutline->SetActiveCamera(renderer->GetActiveCamera());
  if (!renderer->GetRenderWindow()->HasRenderer(this->Internal->RendererOutline))
    {
    renderer->GetRenderWindow()->AddRenderer(this->Internal->RendererOutline);
    }
  this->SetUpdateFromMRMLRequested(false);
  this->RequestRender();
}
