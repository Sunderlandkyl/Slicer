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
  static vtkRendererUpdateObserver* New() { return new vtkRendererUpdateObserver; }
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
    int LabelIndex{ 0 };
    std::string Key;           // nodeID#index
    double AnchorPosition[3];  // World coordinates
    double DisplayPosition[2]; // Display coordinates
    int AssignedPosition[2];   // Final position after collision avoidance

    // Caching to reduce per-frame work
    std::string CachedText;
    int CachedFontSize{0};
    double CachedTextWidth{0.0};
    double CachedTextHeight{0.0};
    int CachedViewportSize[2]{0,0};
    bool SizeDirty{true};
    bool StyleDirty{true};

    int PrevAssignedPosition[2]{INT_MIN, INT_MIN};
    double PrevAnchorDisplay[2]{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
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

  // Helper to safely get renderer (uses cached pointer instead of calling through External)
  vtkRenderer* GetRenderer() { return this->ObservedRenderer; }

  vtkSmartPointer<vtkRendererUpdateObserver> RendererUpdateObserver;
  vtkWeakPointer<vtkRenderer> ObservedRenderer;
  unsigned long RendererUpdateObservationId{ 0 };
};

//---------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::vtkInternal(vtkMRMLNodeLabelsDisplayableManager3D* external)
{
  this->External = external;
  this->RendererUpdateObserver = vtkSmartPointer<vtkRendererUpdateObserver>::New();
  this->RendererUpdateObserver->DisplayableManager = external;
}

//---------------------------------------------------------------------------
vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::~vtkInternal()
{
  // Remove observer first to prevent callbacks during destruction
  this->RemoveRendererUpdateObserver();
  this->RemoveAllLabels();
  this->RendererUpdateObserver = nullptr;
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::AddLabel(vtkMRMLLabelDisplayNode* displayNode)
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

    // Create line actor (bounding edge + connector). 4 points, 2 line segments.
    info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
    vtkNew<vtkPoints> linePoints;
    linePoints->SetNumberOfPoints(4); // 0-1: bounding edge, 2-3: connector
    info.LinePolyData->SetPoints(linePoints);
    vtkNew<vtkCellArray> lines;
    {
      vtkNew<vtkLine> edgeLine; // bounding edge
      edgeLine->GetPointIds()->SetId(0, 0);
      edgeLine->GetPointIds()->SetId(1, 1);
      lines->InsertNextCell(edgeLine);
      vtkNew<vtkLine> connectorLine; // anchor to projection point
      connectorLine->GetPointIds()->SetId(0, 2);
      connectorLine->GetPointIds()->SetId(1, 3);
      lines->InsertNextCell(connectorLine);
    }
    info.LinePolyData->SetLines(lines);
    info.LineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
    info.LineMapper->SetInputData(info.LinePolyData);
    info.LineActor = vtkSmartPointer<vtkActor2D>::New();
    info.LineActor->SetMapper(info.LineMapper);
    info.LineActor->GetProperty()->SetLineWidth(2.0);

    if (this->GetRenderer())
    {
      this->GetRenderer()->AddActor2D(info.TextActor);
      this->GetRenderer()->AddActor2D(info.LineActor);
    }

    this->Labels[info.Key] = info;
  }

  this->UpdateLabel(displayNode);
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLabel(vtkMRMLLabelDisplayNode* displayNode)
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
  for (const auto& itPair : this->Labels)
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
      if (this->GetRenderer())
      {
        this->GetRenderer()->RemoveActor2D(li.TextActor);
        this->GetRenderer()->RemoveActor2D(li.LineActor);
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
  info.TextActor->SetTextScaleModeToViewport();
      info.TextActor->GetTextProperty()->ShallowCopy(baseInfo.TextPropertyPtr);
      info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
      vtkNew<vtkPoints> linePoints;
      linePoints->SetNumberOfPoints(4); // 0-1 bounding edge, 2 anchor, 3 projection
      info.LinePolyData->SetPoints(linePoints);
      vtkNew<vtkCellArray> lines;
      {
        vtkNew<vtkLine> edgeLine;
        edgeLine->GetPointIds()->SetId(0, 0);
        edgeLine->GetPointIds()->SetId(1, 1);
        lines->InsertNextCell(edgeLine);
        vtkNew<vtkLine> connectorLine;
        connectorLine->GetPointIds()->SetId(0, 2);
        connectorLine->GetPointIds()->SetId(1, 3);
        lines->InsertNextCell(connectorLine);
      }
      info.LinePolyData->SetLines(lines);
      info.LineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
      info.LineMapper->SetInputData(info.LinePolyData);
      info.LineActor = vtkSmartPointer<vtkActor2D>::New();
      info.LineActor->SetMapper(info.LineMapper);
      info.LineActor->GetProperty()->SetLineWidth(2.0);
      if (this->GetRenderer())
      {
        this->GetRenderer()->AddActor2D(info.TextActor);
        this->GetRenderer()->AddActor2D(info.LineActor);
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

    // Text content
    if (info.CachedText != baseInfo.Text)
    {
      info.TextActor->SetInput(baseInfo.Text.c_str());
      info.CachedText = baseInfo.Text;
      info.SizeDirty = true;
    }

    // Font size mapping
    int fontSize = static_cast<int>(std::max(1.0, baseInfo.TextScale * 10.0));
    if (info.CachedFontSize != fontSize)
    {
      info.TextActor->GetTextProperty()->SetFontSize(fontSize);
      info.CachedFontSize = fontSize;
      info.SizeDirty = true;
    }

    // Color (cheap)
    info.TextActor->GetTextProperty()->SetColor(baseInfo.Color);
    info.LineActor->GetProperty()->SetColor(baseInfo.Color);

    // Visibility
    if (info.TextActor->GetVisibility() != (baseInfo.Visible ? 1 : 0))
    {
      info.TextActor->SetVisibility(baseInfo.Visible);
    }
    int lineVis = (baseInfo.Visible && baseInfo.LineVisible) ? 1 : 0;
    if (info.LineActor->GetVisibility() != lineVis)
    {
      info.LineActor->SetVisibility(lineVis);
    }

    // Anchor/display
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
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::RemoveLabel(vtkMRMLLabelDisplayNode* displayNode)
{
  if (!displayNode || !displayNode->GetID())
  {
    return;
  }

  const std::string nodeID = displayNode->GetID();
  std::vector<std::string> toErase;
  for (const auto& it : this->Labels)
  {
    if (it.second.DisplayNode == displayNode)
    {
      if (this->GetRenderer())
      {
        this->GetRenderer()->RemoveActor2D(it.second.TextActor);
        this->GetRenderer()->RemoveActor2D(it.second.LineActor);
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
    if (this->GetRenderer())
    {
      this->GetRenderer()->RemoveActor2D(info.TextActor);
      this->GetRenderer()->RemoveActor2D(info.LineActor);
    }
  }
  this->Labels.clear();
}

//---------------------------------------------------------------------------
// (Removed segmentation-specific anchor calculations; anchor now comes from display node label info)

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::WorldToDisplay(const double worldPos[3], double displayPos[2])
{
  if (!this->GetRenderer())
  {
    displayPos[0] = 0;
    displayPos[1] = 0;
    return;
  }

  // Use VTK coordinate conversion
  vtkNew<vtkCoordinate> coordinate;
  coordinate->SetCoordinateSystemToWorld();
  coordinate->SetValue(worldPos[0], worldPos[1], worldPos[2]);
  int* display = coordinate->GetComputedDisplayValue(this->GetRenderer());
  displayPos[0] = display[0];
  displayPos[1] = display[1];
}

//---------------------------------------------------------------------------
void vtkMRMLNodeLabelsDisplayableManager3D::vtkInternal::UpdateLabelPositions()
{
  if (!this->GetRenderer())
  {
    return;
  }

  int* viewportSize = this->GetRenderer()->GetSize();
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

    // Update cached viewport change -> size dirty
    if (this->GetRenderer())
    {
      int* vs = this->GetRenderer()->GetSize();
      if (vs[0] != info.CachedViewportSize[0] || vs[1] != info.CachedViewportSize[1])
      {
        info.CachedViewportSize[0] = vs[0];
        info.CachedViewportSize[1] = vs[1];
        info.SizeDirty = true;
      }
    }

    int labelPosition = baseInfo.LabelPosition;
    // Get cached text size (compute lazily if dirty)
    if (info.SizeDirty)
    {
      double bbox[4] = { 0, 0, 0, 0 };
      info.TextActor->GetBoundingBox(this->GetRenderer(), bbox);
      info.CachedTextWidth = std::max(0.0, bbox[1] - bbox[0]);
      info.CachedTextHeight = std::max(0.0, bbox[3] - bbox[2]);
      info.SizeDirty = false;
    }
    int textW = static_cast<int>(info.CachedTextWidth);
    int textH = static_cast<int>(info.CachedTextHeight);
    switch (labelPosition)
    {
      case vtkMRMLLabelDisplayNode::LabelPositionLeft:
        info.AssignedPosition[0] = margin;
        info.AssignedPosition[1] = static_cast<int>(info.DisplayPosition[1]);
        info.TextActor->GetTextProperty()->SetJustificationToLeft();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToCentered();
        break;
      case vtkMRMLLabelDisplayNode::LabelPositionRight:
        info.AssignedPosition[0] = viewportSize[0] - margin - textW;
        info.AssignedPosition[1] = static_cast<int>(info.DisplayPosition[1]);
        info.TextActor->GetTextProperty()->SetJustificationToLeft(); // manual x placement
        info.TextActor->GetTextProperty()->SetVerticalJustificationToCentered();
        break;
      case vtkMRMLLabelDisplayNode::LabelPositionTop:
        info.AssignedPosition[0] = static_cast<int>(info.DisplayPosition[0]);
        info.AssignedPosition[1] = viewportSize[1] - margin - textH;
        info.TextActor->GetTextProperty()->SetJustificationToCentered();
        info.TextActor->GetTextProperty()->SetVerticalJustificationToBottom();
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

  // Collision avoidance after initial assignments
  const int minGap = 4; // pixels
  auto adjustGroup = [&](int side, bool verticalStack)
  {
    std::vector<LabelInfo*> group;
    for (auto& kv : this->Labels)
    {
      vtkMRMLLabelDisplayNode::LabelInfo bi;
      if (!kv.second.DisplayNode)
      {
        continue;
      }
      kv.second.DisplayNode->GetLabelInfo(kv.second.LabelIndex, bi);
      if (bi.LabelPosition == side && kv.second.TextActor->GetVisibility())
      {
        group.push_back(&kv.second);
      }
    }
    if (group.size() < 2)
    {
      return;
    }
    if (verticalStack)
    {
      std::sort(group.begin(), group.end(), [](LabelInfo* a, LabelInfo* b) { return a->AssignedPosition[1] < b->AssignedPosition[1]; });
      for (size_t i = 1; i < group.size(); ++i)
      {
        int prevH = static_cast<int>(group[i - 1]->CachedTextHeight);
        int neededY = group[i - 1]->AssignedPosition[1] + prevH + minGap;
        if (group[i]->AssignedPosition[1] < neededY)
        {
          group[i]->AssignedPosition[1] = neededY;
        }
      }
    }
    else
    {
      std::sort(group.begin(), group.end(), [](LabelInfo* a, LabelInfo* b) { return a->AssignedPosition[0] < b->AssignedPosition[0]; });
      for (size_t i = 1; i < group.size(); ++i)
      {
        int prevW = static_cast<int>(group[i - 1]->CachedTextWidth);
        int neededX = group[i - 1]->AssignedPosition[0] + prevW + minGap;
        if (group[i]->AssignedPosition[0] < neededX)
        {
          group[i]->AssignedPosition[0] = neededX;
        }
      }
    }
  };

  adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionLeft, true);
  adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionRight, true);
  adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionTop, false);
  adjustGroup(vtkMRMLLabelDisplayNode::LabelPositionBottom, false);

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

    // Only update actor position if changed
    if (info.PrevAssignedPosition[0] != info.AssignedPosition[0]
      || info.PrevAssignedPosition[1] != info.AssignedPosition[1])
    {
      info.TextActor->SetDisplayPosition(info.AssignedPosition[0], info.AssignedPosition[1]);
      info.PrevAssignedPosition[0] = info.AssignedPosition[0];
      info.PrevAssignedPosition[1] = info.AssignedPosition[1];
    }

    // Update line geometry
    if (info.PrevAnchorDisplay[0] != info.DisplayPosition[0]
      || info.PrevAnchorDisplay[1] != info.DisplayPosition[1]
      || info.PrevAssignedPosition[0] != info.AssignedPosition[0]
      || info.PrevAssignedPosition[1] != info.AssignedPosition[1]
      || info.SizeDirty)
    {
      this->UpdateLineGeometry(info);
      info.PrevAnchorDisplay[0] = info.DisplayPosition[0];
      info.PrevAnchorDisplay[1] = info.DisplayPosition[1];
    }
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

  vtkPoints* points = label.LinePolyData->GetPoints();
  if (!points || points->GetNumberOfPoints() < 4)
  {
    // Unexpected polydata layout; skip
    return;
  }

  // Determine which side bounding edge should be drawn on (opposite of screen edge)
  int labelPosition = baseInfo.LabelPosition;
  // Use cached text size and derive bounding box from assigned position
  double textW = label.CachedTextWidth;
  double textH = label.CachedTextHeight;
  double leftX = static_cast<double>(label.AssignedPosition[0]);
  double bottomY = static_cast<double>(label.AssignedPosition[1]);
  double rightX = leftX + textW;
  double topY = bottomY + textH;

  // Bounding edge endpoints (A,B)
  double Ax = 0, Ay = 0, Bx = 0, By = 0;
  switch (labelPosition)
  {
    case vtkMRMLLabelDisplayNode::LabelPositionLeft: // bounding line on right side of text
      Ax = rightX;
      Ay = bottomY;
      Bx = rightX;
      By = topY;
      break;
    case vtkMRMLLabelDisplayNode::LabelPositionRight: // bounding line on left side
      Ax = leftX;
      Ay = bottomY;
      Bx = leftX;
      By = topY;
      break;
    case vtkMRMLLabelDisplayNode::LabelPositionTop: // bounding line bottom side
      Ax = leftX;
      Ay = bottomY;
      Bx = rightX;
      By = bottomY;
      break;
    case vtkMRMLLabelDisplayNode::LabelPositionBottom: // bounding line top side
      Ax = leftX;
      Ay = topY;
      Bx = rightX;
      By = topY;
      break;
    default:
      // Default: no bounding edge; draw simple connector only
      points->SetPoint(0, label.DisplayPosition[0], label.DisplayPosition[1], 0.0);
      points->SetPoint(1, label.AssignedPosition[0], label.AssignedPosition[1], 0.0);
      points->SetPoint(2, label.DisplayPosition[0], label.DisplayPosition[1], 0.0);
      points->SetPoint(3, label.AssignedPosition[0], label.AssignedPosition[1], 0.0);
      points->Modified();
      label.LinePolyData->Modified();
      return;
  }

  // Anchor display position (world->display) is label.DisplayPosition
  double anchorX = label.DisplayPosition[0];
  double anchorY = label.DisplayPosition[1];

  // Project anchor onto bounding edge segment (A,B)
  double ABx = Bx - Ax;
  double ABy = By - Ay;
  double ABlen2 = ABx * ABx + ABy * ABy;
  double t = 0.0;
  if (ABlen2 > 0.0)
  {
    double APx = anchorX - Ax;
    double APy = anchorY - Ay;
    t = (APx * ABx + APy * ABy) / ABlen2;
    if (t < 0.0)
    {
      t = 0.0;
    }
    else if (t > 1.0)
    {
      t = 1.0;
    }
  }
  double projX = Ax + t * ABx;
  double projY = Ay + t * ABy;

  // Fallback: if projected point very close to anchor, connect to text box center for visibility
  double centerX = (leftX + rightX) * 0.5;
  double centerY = (bottomY + topY) * 0.5;
  double dx = projX - anchorX;
  double dy = projY - anchorY;
  double dist2 = dx*dx + dy*dy;
  if (dist2 < 9.0) // <3px length
  {
    projX = centerX;
    projY = centerY;
  }

  // Set polydata points: 0-1 bounding edge, 2 anchor, 3 projection (or center fallback)
  points->SetPoint(0, Ax, Ay, 0.0);
  points->SetPoint(1, Bx, By, 0.0);
  points->SetPoint(2, anchorX, anchorY, 0.0);
  points->SetPoint(3, projX, projY, 0.0);
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
  // Safety check: don't update if being destroyed
  if (!this->Internal || !this->GetRenderer())
  {
    return;
  }
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
void vtkMRMLNodeLabelsDisplayableManager3D::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
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
