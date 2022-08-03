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

// MRMLDM includes
#include "vtkMRMLFocusRepresentation.h"

// MRML includes
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLSelectionNode.h>

// VTK includes
#include <vtkActor2D.h>
#include <vtkOutlineGlowPass.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkRenderer.h>
#include <vtkRenderStepsPass.h>

class SoftFocusDisplayPipeline
{
  public:

    //----------------------------------------------------------------------
    SoftFocusDisplayPipeline()
    {
      this->ROIGlowPass->SetDelegatePass(this->BasicPasses);
      this->RendererOutline->UseFXAAOn();
      this->RendererOutline->UseShadowsOff();
      this->RendererOutline->UseDepthPeelingOff();
      this->RendererOutline->UseDepthPeelingForVolumesOff();
      this->RendererOutline->SetPass(this->ROIGlowPass);
    }

    //----------------------------------------------------------------------
    virtual ~SoftFocusDisplayPipeline()
    {
    }

    vtkNew<vtkRenderer> RendererOutline;
    vtkNew<vtkRenderStepsPass> BasicPasses;
    vtkNew<vtkOutlineGlowPass> ROIGlowPass;

};

//---------------------------------------------------------------------------
class vtkMRMLFocusRepresentation::vtkInternal
{
  public:
    vtkInternal(vtkMRMLFocusRepresentation* external);
    ~vtkInternal();

    vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;

    SoftFocusDisplayPipeline* SoftFocusPipeline;

    //vtkNew<vtkPolyData>         CornerROIPolyData;
    //vtkNew<vtkPolyDataMapper2D> CornerROIMapper;
    //vtkNew<vtkActor2D>          CornerROIActor;

    std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> DisplayableNodes;
    std::vector<vtkSmartPointer<vtkProp>> OriginalActors;
    std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> OriginalToCopyActors;

    vtkMRMLFocusRepresentation* External;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkInternal
::vtkInternal(vtkMRMLFocusRepresentation* external)
{
  this->External = external;
  this->SoftFocusPipeline = new SoftFocusDisplayPipeline();
  //this->CornerROIMapper->SetInputData(this->CornerROIPolyData);
  //this->CornerROIActor->SetMapper(this->CornerROIMapper);
}

//---------------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkInternal::~vtkInternal()
{
  this->External = nullptr;
  delete this->SoftFocusPipeline;
  this->SoftFocusPipeline = nullptr;
}

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLFocusRepresentation);

//----------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkMRMLFocusRepresentation()
  : Internal(new vtkInternal(this))
{
}

//----------------------------------------------------------------------
vtkMRMLFocusRepresentation::~vtkMRMLFocusRepresentation()
{
  delete this->Internal;
}

//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::GetActors(vtkPropCollection* pc)
{
  for (auto oldNewActorPair : this->Internal->OriginalToCopyActors)
    {
    vtkProp* newActor = oldNewActorPair.second;
    pc->AddItem(newActor);
    }
}


//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::GetActors2D(vtkPropCollection * pc)
{
}

//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::ReleaseGraphicsResources(vtkWindow * win)
{
}

//----------------------------------------------------------------------
int vtkMRMLFocusRepresentation::RenderOverlay(vtkViewport * viewport)
{
  int count = 0;
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLFocusRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  int count = 0;
  return count;
}

//----------------------------------------------------------------------
int vtkMRMLFocusRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = 0;
  return count;
}

//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::PrintSelf(ostream & os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
std::string vtkMRMLFocusRepresentation::CanInteract(vtkMRMLInteractionEventData* interactionEventData,
  int& foundComponentType, int& foundComponentIndex, double& closestDistance2, double& handleOpacity)
{
  return "";
}

//-----------------------------------------------------------------------------
void vtkMRMLFocusRepresentation::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void* callData = nullptr)
{
  SoftFocusDisplayPipeline* sfPipeline = this->Internal->SoftFocusPipeline;
  sfPipeline->RendererOutline->RemoveAllViewProps();

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

    vtkActor2D* newActor2D = vtkActor2D::SafeDownCast(prop);
    if (newActor2D)
      {
      broker->AddObservation(newActor2D->GetPositionCoordinate(),
        vtkCommand::ModifiedEvent,
        this, this->GetMRMLNodesCallbackCommand());
      }

    }
  this->Internal->OriginalToCopyActors = newOriginalToCopyActors;

  this->UpdateActors();

  this->Internal->RendererOutline->SetActiveCamera(renderer->GetActiveCamera());
  if (!renderer->GetRenderWindow()->HasRenderer(this->Internal->RendererOutline))
    {
    renderer->GetRenderWindow()->AddRenderer(this->Internal->RendererOutline);
    }
  this->SetUpdateFromMRMLRequested(false);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusRepresentation::UpdateActors()
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
void vtkMRMLFocusRepresentation::UpdateActor(vtkProp* originalProp)
{
  vtkProp* copyProp = this->Internal->OriginalToCopyActors[originalProp];
  if (!copyProp)
    {
    return;
    }

  //// Copy the properties of the original actor to the duplicate one
  //copyProp->ShallowCopy(originalProp);

  //vtkActor* copyActor = vtkActor::SafeDownCast(copyProp);
  //if (copyActor)
  //  {
  //  copyActor->SetTexture(nullptr);

  //  // Make the actor flat. This generates a better outline.
  //  vtkSmartPointer<vtkProperty> copyProperty = vtkSmartPointer<vtkProperty>::Take(copyActor->GetProperty()->NewInstance());
  //  copyProperty->DeepCopy(copyActor->GetProperty());
  //  copyProperty->SetLighting(false);
  //  copyProperty->SetColor(1.0, 1.0, 1.0);
  //  copyProperty->SetOpacity(1.0);
  //  copyActor->SetProperty(copyProperty);
  //  }

  //vtkVolume* copyVolume = vtkVolume::SafeDownCast(copyProp);
  //if (copyVolume)
  //  {
  //  vtkNew<vtkColorTransferFunction> colorTransferFunction;
  //  colorTransferFunction->AddRGBPoint(0, 1.0, 1.0, 1.0);

  //  vtkSmartPointer<vtkVolumeProperty> copyProperty = vtkSmartPointer<vtkVolumeProperty>::Take(copyVolume->GetProperty()->NewInstance());
  //  copyProperty->DeepCopy(copyVolume->GetProperty());
  //  copyProperty->SetDiffuse(0.0);
  //  copyProperty->SetAmbient(1.0);
  //  copyProperty->ShadeOff();
  //  copyProperty->SetColor(colorTransferFunction);
  //  copyVolume->SetProperty(copyProperty);
  //  }

  //vtkActor2D* copyActor2D = vtkActor2D::SafeDownCast(copyProp);
  //if (copyActor2D)
  //  {
  //  vtkSmartPointer<vtkProperty2D> copyProperty2D = vtkSmartPointer<vtkProperty2D>::Take(copyActor2D->GetProperty()->NewInstance());
  //  copyProperty2D->DeepCopy(copyActor2D->GetProperty());
  //  copyProperty2D->SetColor(1.0, 1.0, 1.0);
  //  copyActor2D->SetProperty(copyProperty2D);
  //  }

  //vtkLabelPlacementMapper* oldLabelMapper = copyActor2D ? vtkLabelPlacementMapper::SafeDownCast(copyActor2D->GetMapper()) : nullptr;
  //if (oldLabelMapper)
  //  {
  //  // TODO: Workaround for markups widgets in order to modify text property for control point labels.

  //  vtkPointSetToLabelHierarchy* oldPointSetInput = vtkPointSetToLabelHierarchy::SafeDownCast(oldLabelMapper->GetInputAlgorithm());
  //  if (oldPointSetInput)
  //    {
  //    vtkSmartPointer<vtkLabelPlacementMapper> newLabelMapper = vtkSmartPointer<vtkLabelPlacementMapper>::Take(oldLabelMapper->NewInstance());
  //    newLabelMapper->ShallowCopy(oldLabelMapper);

  //    vtkSmartPointer<vtkPointSetToLabelHierarchy> newPointSetInput = vtkSmartPointer<vtkPointSetToLabelHierarchy>::Take(oldPointSetInput->NewInstance());
  //    newPointSetInput->SetInputData(oldPointSetInput->GetInput());
  //    newPointSetInput->SetLabelArrayName("labels");
  //    newPointSetInput->SetPriorityArrayName("priority");

  //    vtkSmartPointer<vtkTextProperty> textProperty = vtkSmartPointer<vtkTextProperty>::Take(newPointSetInput->GetTextProperty()->NewInstance());
  //    textProperty->ShallowCopy(newPointSetInput->GetTextProperty());
  //    textProperty->SetBackgroundRGBA(1.0, 1.0, 1.0, 1.0);
  //    newPointSetInput->SetTextProperty(textProperty);

  //    newLabelMapper->SetInputConnection(newPointSetInput->GetOutputPort());

  //    copyActor2D->SetMapper(newLabelMapper);
  //    }
  //  }

  //vtkTextActor* copyTextActor = vtkTextActor::SafeDownCast(copyProp);
  //if (copyTextActor)
  //  {
  //  // TODO: Outline is not large enough if background is fully transparent.
  //  vtkSmartPointer<vtkTextProperty> copyTextProperty = vtkSmartPointer<vtkTextProperty>::Take(copyTextActor->GetTextProperty()->NewInstance());
  //  copyTextProperty->ShallowCopy(copyTextActor->GetTextProperty());
  //  copyTextProperty->SetBackgroundRGBA(1.0, 1.0, 1.0, 1.0);
  //  copyTextActor->SetTextProperty(copyTextProperty);
  //  }
 }
