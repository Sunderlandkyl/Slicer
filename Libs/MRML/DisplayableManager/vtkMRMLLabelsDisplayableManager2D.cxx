/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// MRMLDisplayableManager includes
#include "vtkMRMLLabelsDisplayableManager2D.h"
#include "vtkMRMLLabelDisplayNode.h"
#include "vtkMRMLLabelsWidget.h"
#include "vtkMRMLLabelsWidgetRepresentation2D.h"

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLSliceNode.h>
#include <vtkMRMLSegmentationLabelDisplayNode.h>
#include <vtkMRMLDisplayableManagerGroup.h>
#include <vtkMRMLDisplayableNode.h>

// Forward declare the segmentation displayable manager class
// We'll use runtime type checking instead of compile-time includes
class vtkMRMLSegmentationsDisplayableManager2D;

// VTK includes
#include <vtkCommand.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>

// STD includes
#include <map>

//----------------------------------------------------------------------------
// Renderer update observer callback
class vtkRendererUpdateObserver2D : public vtkCommand
{
public:
  static vtkRendererUpdateObserver2D* New() { return new vtkRendererUpdateObserver2D; }
  void Execute(vtkObject* vtkNotUsed(wdg), unsigned long vtkNotUsed(event), void* vtkNotUsed(calldata)) override
  {
    if (this->DisplayableManager)
    {
      this->DisplayableManager->UpdateFromRenderer();
    }
  }
  vtkWeakPointer<vtkMRMLLabelsDisplayableManager2D> DisplayableManager;
};

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLLabelsDisplayableManager2D);

//----------------------------------------------------------------------------
// vtkInternal helper class

//---------------------------------------------------------------------------
class vtkMRMLLabelsDisplayableManager2D::vtkInternal
{
public:
  vtkInternal(vtkMRMLLabelsDisplayableManager2D* external);
  ~vtkInternal();

  typedef std::map<vtkMRMLLabelDisplayNode*, vtkSmartPointer<vtkMRMLLabelsWidget>> WidgetMapType;
  WidgetMapType Widgets;

  vtkMRMLLabelsDisplayableManager2D* External;

  void AddRendererUpdateObserver(vtkRenderer* renderer);
  void RemoveRendererUpdateObserver();

  vtkSmartPointer<vtkRendererUpdateObserver2D> RendererUpdateObserver;
  vtkWeakPointer<vtkRenderer> ObservedRenderer;
  unsigned long RendererUpdateObservationId{ 0 };
};

//---------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager2D::vtkInternal::vtkInternal(vtkMRMLLabelsDisplayableManager2D* external)
{
  this->External = external;
  this->RendererUpdateObserver = vtkSmartPointer<vtkRendererUpdateObserver2D>::New();
  this->RendererUpdateObserver->DisplayableManager = external;
}

//---------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager2D::vtkInternal::~vtkInternal()
{
  // Remove observer first to prevent callbacks during destruction
  this->RemoveRendererUpdateObserver();
  this->Widgets.clear();
}

//---------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::vtkInternal::AddRendererUpdateObserver(vtkRenderer* renderer)
{
  RemoveRendererUpdateObserver();
  if (renderer)
  {
    this->ObservedRenderer = renderer;
    this->RendererUpdateObservationId = renderer->AddObserver(vtkCommand::StartEvent, this->RendererUpdateObserver);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::vtkInternal::RemoveRendererUpdateObserver()
{
  if (this->ObservedRenderer)
  {
    this->ObservedRenderer->RemoveObserver(this->RendererUpdateObservationId);
    this->ObservedRenderer = nullptr;
    this->RendererUpdateObservationId = 0;
  }
}

//----------------------------------------------------------------------------
// vtkMRMLLabelsDisplayableManager2D methods

//----------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager2D::vtkMRMLLabelsDisplayableManager2D()
{
  this->Internal = new vtkInternal(this);
}

//----------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager2D::~vtkMRMLLabelsDisplayableManager2D()
{
  delete this->Internal;
  this->Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "NodeLabelsDisplayableManager2D" << std::endl;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::Create()
{
  this->Superclass::Create();
  this->Internal->AddRendererUpdateObserver(this->GetRenderer());
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::AdditionalInitializeStep()
{
  // Observe all node label display nodes in the scene
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::UpdateFromMRML()
{
  // Called by the displayable manager framework when a render is requested.
  if (!this->Internal || !this->GetRenderer())
  {
    return;
  }

  // Update all widgets
  for (auto& it : this->Internal->Widgets)
  {
    if (it.second)
    {
      it.second->UpdateFromRenderer();
    }
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::UpdateFromRenderer()
{
  // Safety check: don't update if being destroyed
  if (!this->Internal || !this->GetRenderer())
  {
    return;
  }

  // Update all widgets when view changes
  for (auto& it : this->Internal->Widgets)
  {
    if (it.second)
    {
      it.second->UpdateFromRenderer();
    }
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::UnobserveMRMLScene()
{
  this->Internal->Widgets.clear();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  if (!node || !this->GetMRMLScene())
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);

    // Create widget
    vtkMRMLLabelsWidget* widget = this->CreateWidget(displayNode);
    if (widget)
    {
      this->Internal->Widgets[displayNode] = widget;
    }

    // Observe the display node
    vtkNew<vtkIntArray> events;
    events->InsertNextValue(vtkCommand::ModifiedEvent);
    events->InsertNextValue(vtkMRMLLabelDisplayNode::LabelTextModifiedEvent);
    events->InsertNextValue(vtkMRMLLabelDisplayNode::AnchorPositionModifiedEvent);
    events->InsertNextValue(vtkMRMLLabelDisplayNode::LabelPropertiesModifiedEvent);
    vtkObserveMRMLNodeEventsMacro(displayNode, events);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (!node)
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->Internal->Widgets.erase(displayNode);
    vtkUnObserveMRMLNodeMacro(displayNode);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
{
  vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(caller);
  if (displayNode)
  {
    auto it = this->Internal->Widgets.find(displayNode);
    if (it != this->Internal->Widgets.end() && it->second)
    {
      it->second->UpdateFromRenderer();
    }
    // Ensure UpdateFromMRML runs during the next render
    this->SetUpdateFromMRMLRequested(true);
    this->RequestRender();
  }

  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller)
{
  // Let base class handle slice node events
  this->Superclass::OnMRMLDisplayableNodeModifiedEvent(caller);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager2D::OnMRMLSliceNodeModifiedEvent()
{
  // Update all labels when slice view is modified (slice offset, zoom, etc.)
  // Schedule update on next render to avoid redundant updates
  this->SetUpdateFromMRMLRequested(true);
  this->RequestRender();
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidget* vtkMRMLLabelsDisplayableManager2D::CreateWidget(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    return nullptr;
  }

  vtkMRMLLabelsWidget* widget = vtkMRMLLabelsWidget::New();
  widget->SetLabelDisplayNode(displayNode);
  widget->SetRenderer(this->GetRenderer());

  // Create 2D representation
  vtkMRMLLabelsWidgetRepresentation2D* rep = vtkMRMLLabelsWidgetRepresentation2D::New();
  rep->SetRenderer(this->GetRenderer());
  rep->SetLabelDisplayNode(displayNode);
  rep->SetSliceNode(this->GetMRMLSliceNode());
  rep->SetDisplayableManager(this);

  widget->SetRepresentation(rep);
  rep->Delete();

  // Trigger initial update to create label actors
  widget->UpdateFromRenderer();

  return widget;
}

//----------------------------------------------------------------------------
bool vtkMRMLLabelsDisplayableManager2D::GetLabelInfo(vtkMRMLLabelDisplayNode* displayNode,
                                                      int labelIndex,
                                                      vtkMRMLLabelDisplayNode::LabelInfo& info)
{  if (!displayNode)
  {
    return false;
  }

  // First try to get label info from other displayable managers
  // They can provide view-specific information (e.g., slice intersection for segmentations)
  vtkMRMLDisplayableManagerGroup* group = this->GetMRMLDisplayableManagerGroup();
  if (group)
  {
    // Query all displayable managers to see if any can provide label info
    int numDMs = group->GetDisplayableManagerCount();
    for (int i = 0; i < numDMs; ++i)
    {
      vtkMRMLAbstractDisplayableManager* dm =
        vtkMRMLAbstractDisplayableManager::SafeDownCast(group->GetNthDisplayableManager(i));
      if (dm && dm != this)
      {
        // Try to get label info from this displayable manager
        if (dm->GetLabelInfo(displayNode, labelIndex, info))
        {
          // This displayable manager provided the info
          return true;
        }
      }
    }
  }

  // No other displayable manager provided info, get it from the display node
  return displayNode->GetLabelInfo(labelIndex, info);
}
