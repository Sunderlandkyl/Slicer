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

  vtkNew<vtkRenderer> RendererOutline;
  vtkNew<vtkRenderStepsPass> BasicPasses;
  vtkNew<vtkOutlineGlowPass> ROIGlowPass;

  vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;
  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> DisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> OriginalActors;
  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> OriginalToCopyActors;

  vtkNew<vtkPolyData> HardFocusPolyData;
  vtkNew<vtkPolyDataMapper2D> HardFocusMapper;
  vtkNew<vtkActor2D> HardFocusActor;

  vtkNew<vtkCallbackCommand> ObjectCallback;
  static void ObjectsCallback(vtkObject* caller, unsigned long eid,
    void* clientData, void* callData);

  double Bounds_RAS[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusDisplayableManager::vtkInternal::vtkInternal(vtkMRMLFocusDisplayableManager* external)
  : External(external)
{
  this->ROIGlowPass->SetDelegatePass(this->BasicPasses);
  this->RendererOutline->UseFXAAOn();
  this->RendererOutline->UseShadowsOff();
  this->RendererOutline->UseDepthPeelingOff();
  this->RendererOutline->UseDepthPeelingForVolumesOff();
  this->RendererOutline->SetPass(this->ROIGlowPass);

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
  broker->AddObservation(this->External->GetRenderer()->GetActiveCamera(), vtkCommand::ModifiedEvent,
    this->External, this->ObjectCallback);
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
  broker->RemoveObservations(this->External->GetRenderer()->GetActiveCamera(), vtkCommand::InteractionEvent,
    this->External, this->ObjectCallback);
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

  this->UpdateFromMRML();

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
  else if (vtkRenderer::SafeDownCast(caller) || vtkCamera::SafeDownCast(caller))
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
void vtkMRMLFocusDisplayableManager::UpdateFromMRML()
{
  this->UpdateDisplayableNodes();
  this->UpdateOriginalFocusActors();
  this->UpdateSoftFocus();
  this->UpdateHardFocus();
  this->SetUpdateFromMRMLRequested(false);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateDisplayableNodes()
{
  this->Internal->RemoveFocusedNodeObservers();
  this->Internal->DisplayableNodes.clear();

  vtkMRMLDisplayableNode* focusedNode = vtkMRMLDisplayableNode::SafeDownCast(this->GetFocusNode());
  if (focusedNode)
    {
    this->Internal->DisplayableNodes.push_back(focusedNode);
    }

  this->Internal->AddFocusedNodeObservers();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateOriginalFocusActors()
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  for (vtkProp* oldActor : this->Internal->OriginalActors)
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

  this->Internal->OriginalActors.clear();

  std::vector<vtkMRMLDisplayNode*> displayNodes;
  for (vtkMRMLDisplayableNode* displayableNode : this->Internal->DisplayableNodes)
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
        continue;
      }
      displayableManager->GetActorsByID(focusNodeActors, displayNode->GetID(),
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

    this->Internal->OriginalActors.push_back(prop);

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
  this->Internal->RendererOutline->RemoveAllViewProps();

  vtkMRMLSelectionNode* selectionNode = this->Internal->SelectionNode;
  const char* focusNodeID = selectionNode ? selectionNode->GetFocusNodeID() : nullptr;
  vtkMRMLDisplayableNode* focusedNode =
    vtkMRMLDisplayableNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(focusNodeID));

  vtkRenderer* renderer = this->GetRenderer();
  if (!selectionNode || !renderer || !focusedNode || focusedNode->GetNumberOfDisplayNodes() == 0)
    {
    return;
    }

  int RENDERER_LAYER = 1;
  if (renderer->GetRenderWindow()->GetNumberOfLayers() < RENDERER_LAYER + 1)
    {
    renderer->GetRenderWindow()->SetNumberOfLayers(RENDERER_LAYER + 1);
    }

  this->Internal->ROIGlowPass->SetOutlineIntensity(selectionNode->GetFocusedHighlightStrength());
  this->Internal->RendererOutline->SetLayer(RENDERER_LAYER);

  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> newOriginalToCopyActors;

  vtkEventBroker* broker = vtkEventBroker::GetInstance();

  vtkSmartPointer<vtkProp> prop = nullptr;
  vtkCollectionSimpleIterator it = nullptr;
  for (vtkProp* originalProp : this->Internal->OriginalActors)
    {
    if (!originalProp->GetVisibility())
      {
      // Ignore actors that are not visible.
      continue;
      }

    vtkSmartPointer<vtkProp> newProp = this->Internal->OriginalToCopyActors[originalProp];
    if (!newProp)
      {
      newProp = vtkSmartPointer<vtkProp>::Take(originalProp->NewInstance());
      newProp->SetPickable(false);
      }

    newOriginalToCopyActors[originalProp] = newProp;
    this->Internal->RendererOutline->AddViewProp(newProp);
    }
  this->Internal->OriginalToCopyActors = newOriginalToCopyActors;

  this->UpdateActors();

  this->Internal->RendererOutline->SetActiveCamera(renderer->GetActiveCamera());
  if (!renderer->GetRenderWindow()->HasRenderer(this->Internal->RendererOutline))
    {
    renderer->GetRenderWindow()->AddRenderer(this->Internal->RendererOutline);
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

  for (vtkProp* originalProp : this->Internal->OriginalActors)
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

  int index = 0;
  outlinePoints->SetPoint(index++, displayBounds[0] + lenPx, displayBounds[2], 0.0);
  outlinePoints->SetPoint(index++, displayBounds[0], displayBounds[2], 0.0);
  outlinePoints->SetPoint(index++, displayBounds[0], displayBounds[2] + lenPx, 0.0);

  outlinePoints->SetPoint(index++, displayBounds[0], displayBounds[3] - lenPx, 0.0);
  outlinePoints->SetPoint(index++, displayBounds[0], displayBounds[3], 0.0);
  outlinePoints->SetPoint(index++, displayBounds[0] + lenPx, displayBounds[3], 0.0);

  outlinePoints->SetPoint(index++, displayBounds[1] - lenPx, displayBounds[3], 0.0);
  outlinePoints->SetPoint(index++, displayBounds[1], displayBounds[3], 0.0);
  outlinePoints->SetPoint(index++, displayBounds[1], displayBounds[3] - lenPx, 0.0);

  outlinePoints->SetPoint(index++, displayBounds[1], displayBounds[2] + lenPx, 0.0);
  outlinePoints->SetPoint(index++, displayBounds[1], displayBounds[2], 0.0);
  outlinePoints->SetPoint(index++, displayBounds[1] - lenPx, displayBounds[2], 0.0);

  vtkSmartPointer<vtkCellArray> lines = this->Internal->HardFocusPolyData->GetLines();
  if (!lines || lines->GetNumberOfCells() == 0)
    {
    lines = vtkSmartPointer<vtkCellArray>::New();

    index = 0;
    for (int lineIndex = 0; lineIndex < 4; ++lineIndex)
      {
      vtkNew<vtkIdList> corner;
      for (int i = 0; i < 3; ++i)
        {
        corner->InsertNextId(index++);
        }
      lines->InsertNextCell(corner);
      }

    this->Internal->HardFocusPolyData->SetLines(lines);
    }

  outlinePoints->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::UpdateActors()
{
  for (vtkProp* originalProp : this->Internal->OriginalActors)
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
