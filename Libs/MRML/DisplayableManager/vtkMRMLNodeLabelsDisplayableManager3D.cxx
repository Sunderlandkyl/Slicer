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
#include "vtkMRMLNodeLabelsDisplayableManager3D.h"
#include "vtkMRMLLabelDisplayNode.h"

// MRML includes
#include <vtkMRMLDisplayableNode.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSegmentationNode.h>
#include <vtkMRMLViewNode.h>

// VTK includes
#include <vtkActor2D.h>
#include <vtkCellArray.h>
#include <vtkCoordinate.h>
#include <vtkLine.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

// STD includes
#include <algorithm>
#include <map>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Renderer update observer callback
class vtkRendererUpdateObserver : public vtkCommand
{
public:
  static vtkRendererUpdateObserver* New()
  {
    return new vtkRendererUpdateObserver;
  }
  void Execute(vtkObject* vtkNotUsed(wdg), unsigned long vtkNotUsed(event), void* vtkNotUsed(calldata)) override
  {
    if (this->DisplayableManager)
    {
      this->DisplayableManager->UpdateFromRenderer();
    }
  }
  vtkWeakPointer<vtkMRMLNodeLabelsDisplayableManager3D> DisplayableManager;
};

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLNodeLabelsDisplayableManager3D);

//----------------------------------------------------------------------------
// vtkInternal helper class

//---------------------------------------------------------------------------
class vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal
{
public:
  vtkInternal(vtkMRMLNodeLabelsDisplayableManager3D* external);
  ~vtkInternal();

  struct LabelInfo
  {
    vtkSmartPointer<vtkTextActor> TextActor;
    vtkSmartPointer<vtkPolyDataMapper2D> LineMapper;
    vtkSmartPointer<vtkActor2D> LineActor;
    vtkSmartPointer<vtkPolyData> LinePolyData;
    vtkMRMLLabelDisplayNode* DisplayNode;
    int LabelIndex{0};
    std::string Key; // nodeID#index
    double AnchorPosition[3]; // World coordinates
    double DisplayPosition[2]; // Display coordinates
    int AssignedPosition[2];  // Final position after collision avoidance
  };

  typedef std::map<std::string, LabelInfo> LabelsMapType;
  LabelsMapType Labels;

  vtkMRMLNodeLabelsDisplayableManager3D* External;

  void AddLabel(vtkMRMLLabelDisplayNode* displayNode);
  void UpdateLabel(vtkMRMLLabelDisplayNode* displayNode);
  void RemoveLabel(vtkMRMLLabelDisplayNode* displayNode);
  void RemoveAllLabels();

  // Anchor now comes from display node label info
  void WorldToDisplay(const double worldPos[3], double displayPos[2]);

  void UpdateLabelPositions();
  void UpdateLabelActors();
  void UpdateLineGeometry(LabelInfo& label);

  void AddRendererUpdateObserver(vtkRenderer* renderer);
  void RemoveRendererUpdateObserver();

  vtkSmartPointer<vtkRendererUpdateObserver> RendererUpdateObserver;
  vtkWeakPointer<vtkRenderer> ObservedRenderer;
  unsigned long RendererUpdateObservationId{0};
};

//---------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::vtkInternal(
  vtkMRMLNodeLabelsDisplayableManager3D* external)
{
  this->External = external;
  this->RendererUpdateObserver = vtkSmartPointer<vtkRendererUpdateObserver>::New();
  this->RendererUpdateObserver->DisplayableManager = external;
}

//---------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::~vtkInternal()
{
  this->RemoveAllLabels();
  this->RemoveRendererUpdateObserver();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::AddLabel(
  vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode || !displayNode->GetID())
  {
    return;
  }

  // Create actors for each label exposed by the display node
  int count = displayNode->GetNumberOfLabels();
  for (int i = 0; i < count; ++i)
  {
    LabelInfo info;
    info.DisplayNode = displayNode;
    info.LabelIndex = i;

    vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
    bool ok = displayNode->GetLabelInfo(i, baseInfo);

    info.Key = std::string(displayNode->GetID()) + "#" + std::to_string(i);

    // Create text actor
    info.TextActor = vtkSmartPointer<vtkTextActor>::New();
    info.TextActor->SetTextScaleModeToViewport();
    info.TextActor->GetTextProperty()->ShallowCopy(baseInfo.TextPropertyPtr);

    // Create line actor
    info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
    vtkNew<vtkPoints> linePoints;
    linePoints->SetNumberOfPoints(2);
    info.LinePolyData->SetPoints(linePoints);
    vtkNew<vtkCellArray> lines;
    vtkNew<vtkLine> line;
    line->GetPointIds()->SetId(0, 0);
    line->GetPointIds()->SetId(1, 1);
    lines->InsertNextCell(line);
    info.LinePolyData->SetLines(lines);
    info.LineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
    info.LineMapper->SetInputData(info.LinePolyData);
    info.LineActor = vtkSmartPointer<vtkActor2D>::New();
    info.LineActor->SetMapper(info.LineMapper);
    info.LineActor->GetProperty()->SetLineWidth(2.0);

    if (this->External->GetRenderer())
    {
      this->External->GetRenderer()->AddActor2D(info.TextActor);
      this->External->GetRenderer()->AddActor2D(info.LineActor);
    }

    this->Labels[info.Key] = info;
  }

  this->UpdateLabel(displayNode);
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLabel(
  vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode || !displayNode->GetID())
  {
    return;
  }

  // Synchronize number of label actors with display node
  int desired = displayNode->GetNumberOfLabels();
  std::vector<std::string> presentKeys;
  presentKeys.reserve(this->Labels.size());
  const std::string nodeID = displayNode->GetID();
  for (auto const& itPair : this->Labels)
  {
    if (itPair.second.DisplayNode == displayNode)
    {
      presentKeys.push_back(itPair.first);
    }
  }
  for (const std::string& key : presentKeys)
  {
    const LabelInfo& li = this->Labels[key];
    if (li.LabelIndex >= desired)
    {
      if (this->External->GetRenderer())
      {
        this->External->GetRenderer()->RemoveActor2D(li.TextActor);
        this->External->GetRenderer()->RemoveActor2D(li.LineActor);
      }
      this->Labels.erase(key);
    }
  }
  for (int i = 0; i < desired; ++i)
  {
    std::string key = nodeID + "#" + std::to_string(i);
    if (this->Labels.find(key) == this->Labels.end())
    {
      vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
      displayNode->GetLabelInfo(i, baseInfo);

      LabelInfo info;
      info.DisplayNode = displayNode;
      info.LabelIndex = i;
      info.Key = key;
      info.TextActor = vtkSmartPointer<vtkTextActor>::New();
      info.TextActor->SetTextScaleModeToProp();
      info.TextActor->GetTextProperty()->ShallowCopy(baseInfo.TextPropertyPtr);
      info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
      vtkNew<vtkPoints> linePoints;
      linePoints->SetNumberOfPoints(2);
      info.LinePolyData->SetPoints(linePoints);
      vtkNew<vtkCellArray> lines;
      vtkNew<vtkLine> line;
      line->GetPointIds()->SetId(0, 0);
      line->GetPointIds()->SetId(1, 1);
      lines->InsertNextCell(line);
      info.LinePolyData->SetLines(lines);
      info.LineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
      info.LineMapper->SetInputData(info.LinePolyData);
      info.LineActor = vtkSmartPointer<vtkActor2D>::New();
      info.LineActor->SetMapper(info.LineMapper);
      info.LineActor->GetProperty()->SetLineWidth(2.0);
      if (this->External->GetRenderer())
      {
        this->External->GetRenderer()->AddActor2D(info.TextActor);
        this->External->GetRenderer()->AddActor2D(info.LineActor);
      }
      this->Labels[key] = info;
    }
  }

  for (int i = 0; i < desired; ++i)
  {
    std::string key = nodeID + "#" + std::to_string(i);
    LabelInfo& info = this->Labels[key];
    vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
    bool ok = displayNode->GetLabelInfo(i, baseInfo);
    if (!ok)
    {
      info.TextActor->SetVisibility(false);
      info.LineActor->SetVisibility(false);
      continue;
    }
    info.TextActor->SetInput(baseInfo.Text.c_str());
    if (baseInfo.TextPropertyPtr)
    {
      info.TextActor->GetTextProperty()->ShallowCopy(baseInfo.TextPropertyPtr);
    }
    info.TextActor->GetTextProperty()->SetColor(baseInfo.Color);
    info.TextActor->SetVisibility(baseInfo.Visible);
    info.LineActor->SetVisibility(baseInfo.Visible && baseInfo.LineVisible);
    info.LineActor->GetProperty()->SetColor(baseInfo.Color);
    if (baseInfo.Visible)
    {
      info.AnchorPosition[0] = baseInfo.AnchorPosition[0];
      info.AnchorPosition[1] = baseInfo.AnchorPosition[1];
      info.AnchorPosition[2] = baseInfo.AnchorPosition[2];
      this->WorldToDisplay(info.AnchorPosition, info.DisplayPosition);
      info.AssignedPosition[0] = static_cast<int>(info.DisplayPosition[0]);
      info.AssignedPosition[1] = static_cast<int>(info.DisplayPosition[1]);
    }
  }

  this->External->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveLabel(
  vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode || !displayNode->GetID())
  {
    return;
  }

  const std::string nodeID = displayNode->GetID();
  std::vector<std::string> toErase;
  for (auto const& it : this->Labels)
  {
    if (it.second.DisplayNode == displayNode)
    {
      if (this->External->GetRenderer())
      {
        this->External->GetRenderer()->RemoveActor2D(it.second.TextActor);
        this->External->GetRenderer()->RemoveActor2D(it.second.LineActor);
      }
      toErase.push_back(it.first);
    }
  }
  for (const std::string& k : toErase)
  {
    this->Labels.erase(k);
  }
  this->External->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveAllLabels()
{
  LabelsMapType::iterator it;
  for (it = this->Labels.begin(); it != this->Labels.end(); ++it)
  {
    LabelInfo& info = it->second;
    if (this->External->GetRenderer())
    {
      this->External->GetRenderer()->RemoveActor2D(info.TextActor);
      this->External->GetRenderer()->RemoveActor2D(info.LineActor);
    }
  }
  this->Labels.clear();
}

//---------------------------------------------------------------------------
// (Removed segmentation-specific anchor calculations; anchor now comes from display node label info)

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::WorldToDisplay(
  const double worldPos[3], double displayPos[2])
{
  if (!this->External->GetRenderer())
  {
    displayPos[0] = 0;
    displayPos[1] = 0;
    return;
  }

  // Use VTK coordinate conversion
  vtkNew<vtkCoordinate> coordinate;
  coordinate->SetCoordinateSystemToWorld();
  coordinate->SetValue(worldPos[0], worldPos[1], worldPos[2]);
  int* display = coordinate->GetComputedDisplayValue(this->External->GetRenderer());
  displayPos[0] = display[0];
  displayPos[1] = display[1];
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLabelPositions()
{
  if (!this->External->GetRenderer())
  {
    return;
  }

  int* viewportSize = this->External->GetRenderer()->GetSize();
  int margin = 10;

  // Update each label based on its position preference
  for (LabelsMapType::iterator it = this->Labels.begin(); it != this->Labels.end(); ++it)
  {
    LabelInfo& info = it->second;
    vtkMRMLLabelDisplayNode* displayNode = info.DisplayNode;
    if (!displayNode)
    {
      info.TextActor->SetVisibility(false);
      if (info.LineActor)
      {
        info.LineActor->SetVisibility(false);
      }
      continue;
    }

    vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
    if (!displayNode->GetLabelInfo(info.LabelIndex, baseInfo))
    {
      // Hide if label cannot be resolved
      info.TextActor->SetVisibility(false);
      if (info.LineActor)
      {
        info.LineActor->SetVisibility(false);
      }
      continue;
    }

    // Update anchor and display position from latest info
    info.AnchorPosition[0] = baseInfo.AnchorPosition[0];
    info.AnchorPosition[1] = baseInfo.AnchorPosition[1];
    info.AnchorPosition[2] = baseInfo.AnchorPosition[2];
    this->WorldToDisplay(info.AnchorPosition, info.DisplayPosition);

  // Update text actor core properties
  info.TextActor->SetInput(baseInfo.Text.c_str());
  info.TextActor->GetTextProperty()->SetColor(baseInfo.Color[0], baseInfo.Color[1], baseInfo.Color[2]);
  // Map TextScale to font size (base size 12)
  int fontSize = static_cast<int>(std::max(1.0, baseInfo.TextScale * 12.0));
  info.TextActor->GetTextProperty()->SetFontSize(fontSize);
    info.TextActor->SetVisibility(baseInfo.Visible);

    // Line actor visibility
    if (info.LineActor)
    {
      info.LineActor->SetVisibility(baseInfo.Visible && baseInfo.LineVisible);
    }

    int labelPosition = baseInfo.LabelPosition;
    switch (labelPosition)
    {
      case vtkMRMLLabelDisplayNode::LabelPositionLeft:
        info.AssignedPosition[0] = margin;
        info.AssignedPosition[1] = static_cast<int>(info.DisplayPosition[1]);
        info.TextActor->GetTextProperty()->SetJustificationToLeft();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToCentered();
        break;
      case vtkMRMLLabelDisplayNode::LabelPositionRight:
        info.AssignedPosition[0] = viewportSize[0] - margin;
        info.AssignedPosition[1] = static_cast<int>(info.DisplayPosition[1]);
        info.TextActor->GetTextProperty()->SetJustificationToRight();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToCentered();
        break;
      case vtkMRMLLabelDisplayNode::LabelPositionTop:
        info.AssignedPosition[0] = static_cast<int>(info.DisplayPosition[0]);
        info.AssignedPosition[1] = viewportSize[1] - margin;
        info.TextActor->GetTextProperty()->SetJustificationToCentered();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToTop();
        break;
      case vtkMRMLLabelDisplayNode::LabelPositionBottom:
        info.AssignedPosition[0] = static_cast<int>(info.DisplayPosition[0]);
        info.AssignedPosition[1] = margin;
        info.TextActor->GetTextProperty()->SetJustificationToCentered();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToBottom();
        break;
      default:
        info.AssignedPosition[0] = static_cast<int>(info.DisplayPosition[0]);
        info.AssignedPosition[1] = static_cast<int>(info.DisplayPosition[1]);
        info.TextActor->GetTextProperty()->SetJustificationToCentered();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToCentered();
        break;
    }
  }

  this->UpdateLabelActors();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLabelActors()
{
  // Update all label actor positions and line geometry
  for (LabelsMapType::iterator it = this->Labels.begin(); it != this->Labels.end(); ++it)
  {
    LabelInfo& info = it->second;

    if (!info.TextActor->GetVisibility())
    {
      continue;
    }

    // Set text actor position
    info.TextActor->SetDisplayPosition(info.AssignedPosition[0], info.AssignedPosition[1]);

    // Update line geometry
    this->UpdateLineGeometry(info);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLineGeometry(LabelInfo& label)
{
  if (!label.DisplayNode)
  {
    return;
  }

  // Check if line should be visible for this specific label
  vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
  if (!label.DisplayNode->GetLabelInfo(label.LabelIndex, baseInfo) || !baseInfo.LineVisible)
  {
    return;
  }

  // Set line from anchor position to label position
  vtkPoints* points = label.LinePolyData->GetPoints();
  points->SetPoint(0, label.DisplayPosition[0], label.DisplayPosition[1], 0.0);
  points->SetPoint(1, label.AssignedPosition[0], label.AssignedPosition[1], 0.0);
  points->Modified();
  label.LinePolyData->Modified();
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::AddRendererUpdateObserver(vtkRenderer* renderer)
{
  RemoveRendererUpdateObserver();
  if (renderer)
  {
    this->ObservedRenderer = renderer;
    this->RendererUpdateObservationId = renderer->AddObserver(vtkCommand::StartEvent, this->RendererUpdateObserver);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveRendererUpdateObserver()
{
  if (this->ObservedRenderer)
  {
    this->ObservedRenderer->RemoveObserver(this->RendererUpdateObservationId);
    this->ObservedRenderer = nullptr;
    this->RendererUpdateObservationId = 0;
  }
}

//----------------------------------------------------------------------------
// vtkMRMLNodeLabelsDisplayableManager3D methods

//----------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkMRMLNodeLabelsDisplayableManager3D()
{
  this->Internal = new vtkInternal(this);
}

//----------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::~vtkMRMLNodeLabelsDisplayableManager3D()
{
  delete this->Internal;
  this->Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "NodeLabelsDisplayableManager3D" << std::endl;
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::Create()
{
  this->Superclass::Create();
  this->Internal->AddRendererUpdateObserver(this->GetRenderer());
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::AdditionalInitializeStep()
{
  // Observe all node label display nodes in the scene
  /*this->AddMRMLSceneObservation();*/
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::UpdateFromRenderer()
{
  // Update label positions when camera moves
  this->Internal->UpdateLabelPositions();
  this->RequestRender();
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::UnobserveMRMLScene()
{
  this->Internal->RemoveAllLabels();
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  if (!node || !this->GetMRMLScene())
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->Internal->AddLabel(displayNode);

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
void vtkMRMLNodeLabelsDisplayableManager3D::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (!node)
  {
    return;
  }

  if (node->IsA("vtkMRMLLabelDisplayNode"))
  {
    vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(node);
    this->Internal->RemoveLabel(displayNode);
    vtkUnObserveMRMLNodeMacro(displayNode);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::ProcessMRMLNodesEvents(
  vtkObject* caller, unsigned long event, void* callData)
{
  vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(caller);
  if (displayNode)
  {
    this->Internal->UpdateLabel(displayNode);
    this->Internal->UpdateLabelPositions();
    /*this->Internal->UpdateLabelActors();*/
  }

  this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
}

//----------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::OnMRMLDisplayableNodeModifiedEvent(vtkObject* caller)
{
  // Update all labels when view is modified
  this->Internal->UpdateLabelPositions();
  this->RequestRender();
}
