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
#include "vtkMRMLLabelsDisplayableManager3D.h"
#include "vtkMRMLLabelDisplayNode.h"
#include "vtkMRMLLabelsWidget.h"
#include "vtkMRMLLabelsWidgetRepresentation3D.h"

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLViewNode.h>

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
class vtkRendererUpdateObserver3D : public vtkCommand
{
public:
  static vtkRendererUpdateObserver3D* New() { return new vtkRendererUpdateObserver3D; }
  void Execute(vtkObject* vtkNotUsed(wdg), unsigned long vtkNotUsed(event), void* vtkNotUsed(calldata)) override
  {
    if (this->DisplayableManager)
    {
      this->DisplayableManager->UpdateFromRenderer();
    }
  }
  vtkWeakPointer<vtkMRMLLabelsDisplayableManager3D> DisplayableManager;
};

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLLabelsDisplayableManager3D);

//----------------------------------------------------------------------------
// vtkInternal helper class

//---------------------------------------------------------------------------
class vtkMRMLLabelsDisplayableManager3D::vtkInternal
{
public:
  vtkInternal(vtkMRMLLabelsDisplayableManager3D* external);
  ~vtkInternal();

  typedef std::map<vtkMRMLLabelDisplayNode*, vtkSmartPointer<vtkMRMLLabelsWidget>> WidgetMapType;
  WidgetMapType Widgets;

  vtkMRMLLabelsDisplayableManager3D* External;

  void AddRendererUpdateObserver(vtkRenderer* renderer);
  void RemoveRendererUpdateObserver();

  vtkSmartPointer<vtkRendererUpdateObserver3D> RendererUpdateObserver;
  vtkWeakPointer<vtkRenderer> ObservedRenderer;
  unsigned long RendererUpdateObservationId{ 0 };
};

//---------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager3D::vtkInternal::vtkInternal(vtkMRMLLabelsDisplayableManager3D* external)
{
  this->External = external;
  this->RendererUpdateObserver = vtkSmartPointer<vtkRendererUpdateObserver3D>::New();
  this->RendererUpdateObserver->DisplayableManager = external;
}

//---------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager3D::vtkInternal::~vtkInternal()
{
  // Remove observer first to prevent callbacks during destruction
  this->RemoveRendererUpdateObserver();
  this->Widgets.clear();
}

//---------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::vtkInternal::AddRendererUpdateObserver(vtkRenderer* renderer)
{
  RemoveRendererUpdateObserver();
  if (renderer)
  {
    this->ObservedRenderer = renderer;
    this->RendererUpdateObservationId = renderer->AddObserver(vtkCommand::StartEvent, this->RendererUpdateObserver);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::vtkInternal::RemoveRendererUpdateObserver()
{
  if (this->ObservedRenderer)
  {
    this->ObservedRenderer->RemoveObserver(this->RendererUpdateObservationId);
    this->ObservedRenderer = nullptr;
    this->RendererUpdateObservationId = 0;
  }
}

//----------------------------------------------------------------------------
// vtkMRMLLabelsDisplayableManager3D methods

//----------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager3D::vtkMRMLLabelsDisplayableManager3D()
{
  this->Internal = new vtkInternal(this);
}

//----------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager3D::~vtkMRMLLabelsDisplayableManager3D()
{
  delete this->Internal;
  this->Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "NodeLabelsDisplayableManager3D" << std::endl;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::Create()
{
  this->Superclass::Create();
  this->Internal->AddRendererUpdateObserver(this->GetRenderer());
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::AdditionalInitializeStep()
{
  // Observe all node label display nodes in the scene
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::UpdateFromMRML()
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
void vtkMRMLLabelsDisplayableManager3D::UpdateFromRenderer()
{
  // Safety check: don't update if being destroyed
  if (!this->Internal || !this->GetRenderer())
  {
    return;
  }

  // Update all widgets when camera moves
  for (auto& it : this->Internal->Widgets)
  {
    if (it.second)
    {
      it.second->UpdateFromRenderer();
    }
  }

  this->RequestRender();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::UnobserveMRMLScene()
{
  this->Internal->Widgets.clear();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
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
void vtkMRMLLabelsDisplayableManager3D::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
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
void vtkMRMLLabelsDisplayableManager3D::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
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
void vtkMRMLLabelsDisplayableManager3D::OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller)
{
  // Let base class handle view node events
  this->Superclass::OnMRMLDisplayableNodeModifiedEvent(caller);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager3D::OnMRMLViewNodeModifiedEvent()
{
  // Update all labels when view is modified (camera, viewport, etc.)
  // Schedule update on next render to avoid redundant updates
  this->SetUpdateFromMRMLRequested(true);
  this->RequestRender();
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidget* vtkMRMLLabelsDisplayableManager3D::CreateWidget(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    return nullptr;
  }

  vtkMRMLLabelsWidget* widget = vtkMRMLLabelsWidget::New();
  widget->SetLabelDisplayNode(displayNode);
  widget->SetRenderer(this->GetRenderer());

  // Create 3D representation
  vtkMRMLLabelsWidgetRepresentation3D* rep = vtkMRMLLabelsWidgetRepresentation3D::New();
  rep->SetRenderer(this->GetRenderer());
  rep->SetLabelDisplayNode(displayNode);
  rep->SetDisplayableManager(this);

  widget->SetRepresentation(rep);
  rep->Delete();

  // Trigger initial update to create label actors
  widget->UpdateFromRenderer();

  return widget;
}
