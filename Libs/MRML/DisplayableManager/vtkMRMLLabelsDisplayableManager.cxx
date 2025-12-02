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
#include "vtkMRMLLabelsDisplayableManager.h"
#include "vtkMRMLLabelDisplayNode.h"
#include "vtkMRMLLabelsWidget.h"

// MRML includes
#include <vtkMRMLScene.h>

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
class vtkRendererUpdateObserver : public vtkCommand
{
public:
  static vtkRendererUpdateObserver* New() { return new vtkRendererUpdateObserver; }
  void Execute(vtkObject* vtkNotUsed(wdg), unsigned long vtkNotUsed(event), void* vtkNotUsed(calldata)) override
  {
    if (this->DisplayableManager)
    {
      this->DisplayableManager->UpdateFromRenderer();
    }
  }
  vtkWeakPointer<vtkMRMLLabelsDisplayableManager> DisplayableManager;
};

//----------------------------------------------------------------------------
// vtkInternal helper class

//---------------------------------------------------------------------------
class vtkMRMLLabelsDisplayableManager::vtkInternal
{
public:
  vtkInternal(vtkMRMLLabelsDisplayableManager* external);
  ~vtkInternal();

  typedef std::map<vtkMRMLLabelDisplayNode*, vtkSmartPointer<vtkMRMLLabelsWidget>> WidgetMapType;
  WidgetMapType Widgets;

  vtkMRMLLabelsDisplayableManager* External;

  void AddRendererUpdateObserver(vtkRenderer* renderer);
  void RemoveRendererUpdateObserver();

  vtkSmartPointer<vtkRendererUpdateObserver> RendererUpdateObserver;
  vtkWeakPointer<vtkRenderer> ObservedRenderer;
  unsigned long RendererUpdateObservationId{ 0 };
};

//---------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager::vtkInternal::vtkInternal(vtkMRMLLabelsDisplayableManager* external)
{
  this->External = external;
  this->RendererUpdateObserver = vtkSmartPointer<vtkRendererUpdateObserver>::New();
  this->RendererUpdateObserver->DisplayableManager = external;
}

//---------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager::vtkInternal::~vtkInternal()
{
  // Remove observer first to prevent callbacks during destruction
  this->RemoveRendererUpdateObserver();
  this->Widgets.clear();
}

//---------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::vtkInternal::AddRendererUpdateObserver(vtkRenderer* renderer)
{
  RemoveRendererUpdateObserver();
  if (renderer)
  {
    this->ObservedRenderer = renderer;
    this->RendererUpdateObservationId = renderer->AddObserver(vtkCommand::StartEvent, this->RendererUpdateObserver);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::vtkInternal::RemoveRendererUpdateObserver()
{
  if (this->ObservedRenderer)
  {
    this->ObservedRenderer->RemoveObserver(this->RendererUpdateObservationId);
    this->ObservedRenderer = nullptr;
    this->RendererUpdateObservationId = 0;
  }
}

//----------------------------------------------------------------------------
// vtkMRMLLabelsDisplayableManager methods

//----------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager::vtkMRMLLabelsDisplayableManager()
{
  this->Internal = new vtkInternal(this);
}

//----------------------------------------------------------------------------
vtkMRMLLabelsDisplayableManager::~vtkMRMLLabelsDisplayableManager()
{
  delete this->Internal;
  this->Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "LabelsDisplayableManager" << std::endl;
  os << indent << "Number of widgets: " << this->Internal->Widgets.size() << std::endl;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::Create()
{
  this->Superclass::Create();
  this->Internal->AddRendererUpdateObserver(this->GetRenderer());
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::AdditionalInitializeStep()
{
  // Base implementation - subclasses can override
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::UpdateFromMRML()
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
void vtkMRMLLabelsDisplayableManager::UpdateFromRenderer()
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

  this->RequestRender();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::UnobserveMRMLScene()
{
  this->RemoveAllWidgets();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  if (!node || !this->GetMRMLScene())
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->AddWidget(displayNode);

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
void vtkMRMLLabelsDisplayableManager::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (!node)
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->RemoveWidget(displayNode);
    vtkUnObserveMRMLNodeMacro(displayNode);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
{
  vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(caller);
  if (displayNode)
  {
    vtkMRMLLabelsWidget* widget = this->GetWidget(displayNode);
    if (widget)
    {
      widget->UpdateFromRenderer();
    }
    // Ensure UpdateFromMRML runs during the next render
    this->SetUpdateFromMRMLRequested(true);
    this->RequestRender();
  }

  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller)
{
  // Let base class handle view node events
  this->Superclass::OnMRMLDisplayableNodeModifiedEvent(caller);

  // Update all widgets when view is modified
  this->SetUpdateFromMRMLRequested(true);
  this->RequestRender();
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidget* vtkMRMLLabelsDisplayableManager::GetWidget(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    return nullptr;
  }

  auto it = this->Internal->Widgets.find(displayNode);
  if (it != this->Internal->Widgets.end())
  {
    return it->second;
  }
  return nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::AddWidget(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    return;
  }

  // Check if widget already exists
  if (this->GetWidget(displayNode))
  {
    return;
  }

  // Create new widget (subclass-specific)
  vtkMRMLLabelsWidget* widget = this->CreateWidget(displayNode);
  if (!widget)
  {
    return;
  }

  widget->SetLabelDisplayNode(displayNode);
  widget->SetRenderer(this->GetRenderer());
  widget->CreateDefaultRepresentation();

  this->Internal->Widgets[displayNode] = widget;

  // Update widget
  widget->UpdateFromRenderer();
  this->RequestRender();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::RemoveWidget(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode)
  {
    return;
  }

  auto it = this->Internal->Widgets.find(displayNode);
  if (it != this->Internal->Widgets.end())
  {
    this->Internal->Widgets.erase(it);
    this->RequestRender();
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsDisplayableManager::RemoveAllWidgets()
{
  this->Internal->Widgets.clear();
}
