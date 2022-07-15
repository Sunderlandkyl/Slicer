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

  vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;
  vtkNew<vtkMRMLFocusWidget> FocusWidget;

  vtkNew<vtkRenderer> RendererOutline;
  vtkNew<vtkRenderStepsPass> BasicPasses;
  vtkNew<vtkOutlineGlowPass> ROIGlowPass;


  std::vector<vtkWeakPointer<vtkMRMLDisplayableNode>> DisplayableNodes;
  std::vector<vtkSmartPointer<vtkProp>> OriginalActors;
  std::map<vtkSmartPointer<vtkProp>, vtkSmartPointer<vtkProp>> OriginalToCopyActors;

  vtkNew<vtkPolyData>         CornerROIPolyData;
  vtkNew<vtkPolyDataMapper2D> CornerROIMapper;
  vtkNew<vtkActor2D>          CornerROIActor;
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

  this->CornerROIMapper->SetInputData(this->CornerROIPolyData);
  this->CornerROIActor->SetMapper(this->CornerROIMapper);
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
void vtkMRMLFocusDisplayableManager::UpdateSelectionNode()
{
  vtkMRMLScene* mrmlScene = this->GetMRMLScene();
  if (!mrmlScene)
    {
    vtkErrorMacro("UpdateSelectionNode: No MRML scene");
    return;
    }

  vtkMRMLSelectionNode* selectionNode = vtkMRMLSelectionNode::SafeDownCast(mrmlScene->GetFirstNodeByClass("vtkMRMLSelectionNode"));
  if (!selectionNode)
    {
    vtkErrorMacro("UpdateSelectionNode: No selection node");
    return;
    }

  this->SetAndObserveSelectionNode(selectionNode);
}

//---------------------------------------------------------------------------
void vtkMRMLFocusDisplayableManager::SetAndObserveSelectionNode(vtkMRMLSelectionNode* newSelectionNode)
{
  this->Internal->FocusWidget->SetSelectionNode(newSelectionNode);
}
