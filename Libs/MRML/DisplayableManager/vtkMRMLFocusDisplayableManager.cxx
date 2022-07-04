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
#include <vtkMRMLDisplayableManagerGroup.h>
#include <vtkMRMLScene.h>

// VTK includes
#include <vtkAbstractVolumeMapper.h>
#include <vtkActor2D.h>
#include <vtkColorTransferFunction.h>
#include <vtkGPUVolumeRayCastMapper.h>
#include <vtkOutlineGlowPass.h>
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

public:
  vtkMRMLFocusDisplayableManager* External;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::vtkInternal(vtkMRMLFocusDisplayableManager* external)
  : External(external)
{
}

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::~vtkInternal() = default;

//---------------------------------------------------------------------------
// vtkMRMLFocusDisplayableManager methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkMRMLFocusDisplayableManager()
{
  this->Internal = new vtkInternal(this);
  this->ROIGlowPass->SetDelegatePass(this->BasicPasses);
  this->RendererOutline->UseFXAAOn();
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
void vtkMRMLFocusDisplayableManager::UnobserveMRMLScene()
{
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::OnMRMLSceneStartClose()
{
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::OnMRMLSceneEndClose()
{
  this->SetUpdateFromMRMLRequested(true);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateFromMRMLScene()
{
  // UpdateFromMRML will be executed only if there has been some actions
  // during the import that requested it (don't call
  // SetUpdateFromMRMLRequested(1) here, it should be done somewhere else
  // maybe in OnMRMLSceneNodeAddedEvent, OnMRMLSceneNodeRemovedEvent or
  // OnMRMLDisplayableModelNodeModifiedEvent).


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

  bool requestRender = false;
  if (vtkMRMLDisplayableNode::SafeDownCast(caller))
  {
    this->UpdateFromMRML();
  }
  else if (vtkMRMLSelectionNode::SafeDownCast(caller))
  {
    this->UpdateFromMRML();
  }
  else
  {
    this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateFromMRML()
{
  vtkMRMLSelectionNode* selectionNode = this->GetSelectionNode();
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  if (!broker->GetObservationExist(selectionNode, vtkCommand::ModifiedEvent,
    this, this->GetMRMLNodesCallbackCommand()))
  {
    broker->AddObservation(selectionNode, vtkCommand::ModifiedEvent,
      this, this->GetMRMLNodesCallbackCommand());
  }

  this->RendererOutline->RemoveAllViewProps();

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

  this->ROIGlowPass->SetOutlineIntensity(selectionNode->GetHighlightStrength());
  this->RendererOutline->SetLayer(RENDERER_LAYER);
  this->RendererOutline->SetPass(this->ROIGlowPass);

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
      displayableManager->GetActorsByID(displayNode->GetID(), focusNodeActors);
      }
    }

  if (focusNodeActors->GetNumberOfItems() == 0)
    {
    this->SetUpdateFromMRMLRequested(false);
    this->RequestRender();
    return;
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
      vtkSmartPointer<vtkProperty> newProperty = vtkSmartPointer<vtkProperty>::Take(newActor->GetProperty()->NewInstance());;
      newProperty->DeepCopy(newActor->GetProperty());
      newProperty->SetLighting(false);
      newActor->SetProperty(newProperty);
      newProperty->SetColor(1.0, 1.0, 1.0);
    }

    vtkVolume* newVolume = vtkVolume::SafeDownCast(newProp);
    if (newVolume)
    {
      //vtkSmartPointer<vtkAbstractVolumeMapper> newMapper = vtkSmartPointer<vtkAbstractVolumeMapper>::Take(newVolume->GetMapper()->NewInstance());
      //newMapper->ShallowCopy(newVolume->GetMapper());
      //newMapper->SetInputConnection(newVolume->GetMapper()->GetInputConnection(0,0));
      //vtkGPUVolumeRayCastMapper* gpuMapper = vtkGPUVolumeRayCastMapper::SafeDownCast(newMapper);
      //if (gpuMapper)
      //{
      //  /*gpuMapper->SetAutoAdjustSampleDistances(true);*/
      //  //gpuMapper->SetUseDepthPass(false);
      //  //gpuMapper->SetImageSampleDistance(4.0);
      //}
      //newVolume->SetMapper(newMapper);

      vtkSmartPointer<vtkVolumeProperty> newProperty = vtkSmartPointer<vtkVolumeProperty>::Take(newVolume->GetProperty()->NewInstance());;
      newProperty->DeepCopy(newVolume->GetProperty());
      newProperty->SetDiffuse(0.0);
      newProperty->SetAmbient(1.0);
      vtkNew<vtkColorTransferFunction> colorTransferFunction;
      colorTransferFunction->AddRGBPoint(0, 1.0, 1.0, 1.0);
      newProperty->SetColor(colorTransferFunction);
      newProperty->ShadeOff();
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

    this->RendererOutline->AddViewProp(newProp);
  }

  this->RendererOutline->SetActiveCamera(renderer->GetActiveCamera());
  if (!renderer->GetRenderWindow()->HasRenderer(this->RendererOutline))
    {
    renderer->GetRenderWindow()->AddRenderer(this->RendererOutline);
    }
  this->SetUpdateFromMRMLRequested(false);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
}
