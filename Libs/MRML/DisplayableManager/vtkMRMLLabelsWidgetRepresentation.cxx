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

#include "vtkMRMLLabelsWidgetRepresentation.h"

// MRML includes
#include <vtkMRMLLabelDisplayNode.h>
#include "vtkMRMLAbstractDisplayableManager.h"

// VTK includes
#include <vtkActor2D.h>
#include <vtkCellArray.h>
#include <vtkLine.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkTextActor.h>
#include <vtkTextProperty.h>

// STD includes
#include <algorithm>

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation::vtkMRMLLabelsWidgetRepresentation()
{
  this->LabelDisplayNode = nullptr;
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation::~vtkMRMLLabelsWidgetRepresentation()
{
  this->RemoveAllLabels();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Number of labels: " << this->Labels.size() << std::endl;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::SetLabelDisplayNode(vtkMRMLLabelDisplayNode* displayNode)
{
  if (this->LabelDisplayNode == displayNode)
  {
    return;
  }

  this->LabelDisplayNode = displayNode;
  this->UpdateLabels();
  this->Modified();
}

//----------------------------------------------------------------------------
vtkMRMLLabelDisplayNode* vtkMRMLLabelsWidgetRepresentation::GetLabelDisplayNode()
{
  return this->LabelDisplayNode;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::SetDisplayableManager(vtkMRMLAbstractDisplayableManager* dm)
{
  if (this->DisplayableManager == dm)
  {
    return;
  }

  this->DisplayableManager = dm;
  this->Modified();
}

//----------------------------------------------------------------------------
vtkMRMLAbstractDisplayableManager* vtkMRMLLabelsWidgetRepresentation::GetDisplayableManager()
{
  return this->DisplayableManager.GetPointer();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void* callData)
{
  this->Superclass::UpdateFromMRML(caller, event, callData);

  vtkMRMLLabelDisplayNode* displayNode = vtkMRMLLabelDisplayNode::SafeDownCast(caller);
  if (displayNode)
  {
    this->UpdateLabel(displayNode);
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::UpdateLabels()
{
  if (!this->LabelDisplayNode)
  {
    this->RemoveAllLabels();
    return;
  }

  this->UpdateLabel(this->LabelDisplayNode);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::AddLabel(vtkMRMLLabelDisplayNode* displayNode)
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
    info.Key = std::string(displayNode->GetID()) + "#" + std::to_string(i);

    // Create text actor
    info.TextActor = vtkSmartPointer<vtkTextActor>::New();
    info.TextActor->SetTextScaleModeToViewport();

    // Create line actor: bounding edge + connector (4 points, 2 segments)
    info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
    vtkNew<vtkPoints> linePoints;
    linePoints->SetNumberOfPoints(4); // 0-1 edge, 2 anchor, 3 projection
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

    // Add actors to renderer
    if (this->Renderer)
    {
      this->Renderer->AddActor2D(info.TextActor);
      this->Renderer->AddActor2D(info.LineActor);
    }

    this->Labels[info.Key] = info;
  }

  this->UpdateLabel(displayNode);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::UpdateLabel(vtkMRMLLabelDisplayNode* displayNode)
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

  // Remove any actors that exceed desired count
  for (const std::string& key : presentKeys)
  {
    const LabelInfo& li = this->Labels[key];
    if (li.LabelIndex >= desired)
    {
      // Remove actors from renderer
      if (this->Renderer)
      {
        this->Renderer->RemoveActor2D(li.TextActor);
        this->Renderer->RemoveActor2D(li.LineActor);
      }
      this->Labels.erase(key);
    }
  }

  // Ensure we have actors for all needed indices
  for (int i = 0; i < desired; ++i)
  {
    std::string key = nodeID + "#" + std::to_string(i);
    if (this->Labels.find(key) == this->Labels.end())
    {
      // Create new entry
      LabelInfo info;
      info.DisplayNode = displayNode;
      info.LabelIndex = i;
      info.Key = key;

      info.TextActor = vtkSmartPointer<vtkTextActor>::New();
      info.TextActor->SetTextScaleModeToViewport();

      info.LinePolyData = vtkSmartPointer<vtkPolyData>::New();
      vtkNew<vtkPoints> linePoints;
      linePoints->SetNumberOfPoints(4);
      info.LinePolyData->SetPoints(linePoints);
      vtkNew<vtkCellArray> lines;
      vtkNew<vtkLine> edgeLine;
      edgeLine->GetPointIds()->SetId(0, 0);
      edgeLine->GetPointIds()->SetId(1, 1);
      lines->InsertNextCell(edgeLine);
      vtkNew<vtkLine> connectorLine;
      connectorLine->GetPointIds()->SetId(0, 2);
      connectorLine->GetPointIds()->SetId(1, 3);
      lines->InsertNextCell(connectorLine);
      info.LinePolyData->SetLines(lines);
      info.LineMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
      info.LineMapper->SetInputData(info.LinePolyData);
      info.LineActor = vtkSmartPointer<vtkActor2D>::New();
      info.LineActor->SetMapper(info.LineMapper);
      info.LineActor->GetProperty()->SetLineWidth(2.0);

      if (this->Renderer)
      {
        this->Renderer->AddActor2D(info.TextActor);
        this->Renderer->AddActor2D(info.LineActor);
      }
      this->Labels[key] = info;
    }
  }

  // Update properties for all labels
  for (int i = 0; i < desired; ++i)
  {
    std::string key = nodeID + "#" + std::to_string(i);
    LabelInfo& info = this->Labels[key];

    vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
    bool ok = false;
    if (this->DisplayableManager.GetPointer())
    {
      ok = this->DisplayableManager.GetPointer()->GetLabelInfo(displayNode, i, baseInfo);
    }
    else
    {
      // Fallback if no displayable manager is set
      ok = displayNode->GetLabelInfo(i, baseInfo);
    }
    if (!ok)
    {
      info.TextActor->SetVisibility(false);
      info.LineActor->SetVisibility(false);
      continue;
    }

    // Update text & styling only on changes
    if (info.CachedText != baseInfo.Text)
    {
      info.TextActor->SetInput(baseInfo.Text.c_str());
      info.CachedText = baseInfo.Text;
      info.SizeDirty = true;
    }

    int fontSize = static_cast<int>(std::max(1.0, baseInfo.TextScale * 10.0));
    if (info.CachedFontSize != fontSize)
    {
      info.TextActor->GetTextProperty()->SetFontSize(fontSize);
      info.CachedFontSize = fontSize;
      info.SizeDirty = true;
    }

    info.TextActor->GetTextProperty()->SetColor(baseInfo.Color);
    info.LineActor->GetProperty()->SetColor(baseInfo.Color);

    if (info.TextActor->GetVisibility() != (baseInfo.Visible ? 1 : 0))
    {
      info.TextActor->SetVisibility(baseInfo.Visible);
    }

    int lineVis = (baseInfo.Visible && baseInfo.LineVisible) ? 1 : 0;
    if (info.LineActor->GetVisibility() != lineVis)
    {
      info.LineActor->SetVisibility(lineVis);
    }

    // Anchor/display position
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

  this->NeedToRenderOn();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::RemoveLabel(vtkMRMLLabelDisplayNode* displayNode)
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
      // Remove actors from renderer
      if (this->Renderer)
      {
        this->Renderer->RemoveActor2D(it.second.TextActor);
        this->Renderer->RemoveActor2D(it.second.LineActor);
      }
      toErase.push_back(it.first);
    }
  }

  for (const std::string& k : toErase)
  {
    this->Labels.erase(k);
  }

  this->NeedToRenderOn();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::RemoveAllLabels()
{
  for (auto& it : this->Labels)
  {
    LabelInfo& info = it.second;
    if (this->Renderer)
    {
      this->Renderer->RemoveActor2D(info.TextActor);
      this->Renderer->RemoveActor2D(info.LineActor);
    }
  }
  this->Labels.clear();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::UpdateLabelActors()
{
  // Update all label actor positions and line geometry
  for (auto& it : this->Labels)
  {
    LabelInfo& info = it.second;

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

    // Update line geometry only if needed
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

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::UpdateLineGeometry(LabelInfo& label)
{
  if (!label.DisplayNode)
  {
    return;
  }

  // Check if line should be visible for this specific label
  vtkMRMLLabelDisplayNode::LabelInfo baseInfo;
  bool ok = false;
  if (this->DisplayableManager.GetPointer())
  {
    ok = this->DisplayableManager.GetPointer()->GetLabelInfo(label.DisplayNode, label.LabelIndex, baseInfo);
  }
  else
  {
    // Fallback if no displayable manager is set
    ok = label.DisplayNode->GetLabelInfo(label.LabelIndex, baseInfo);
  }
  if (!ok || !baseInfo.LineVisible)
  {
    return;
  }

  vtkPoints* points = label.LinePolyData->GetPoints();
  if (!points || points->GetNumberOfPoints() < 4)
  {
    return;
  }

  int labelPosition = baseInfo.LabelPosition;
  double textW = label.CachedTextWidth;
  double textH = label.CachedTextHeight;
  double leftX = static_cast<double>(label.AssignedPosition[0]);
  double bottomY = static_cast<double>(label.AssignedPosition[1]);
  double rightX = leftX + textW;
  double topY = bottomY + textH;

  double Ax = 0, Ay = 0, Bx = 0, By = 0;
  switch (labelPosition)
  {
    case vtkMRMLLabelDisplayNode::LabelPositionLeft:
      Ax = rightX;
      Ay = bottomY - 0.5 * textH;
      Bx = rightX;
      By = topY - 0.5 * textH;
      break;
    case vtkMRMLLabelDisplayNode::LabelPositionRight:
      Ax = leftX;
      Ay = bottomY - 0.5 * textH;
      Bx = leftX;
      By = topY - 0.5 * textH;
      break;
    case vtkMRMLLabelDisplayNode::LabelPositionTop:
      Ax = leftX - 0.5 * textW;
      Ay = bottomY;
      Bx = rightX - 0.5 * textW;
      By = bottomY;
      break;
    case vtkMRMLLabelDisplayNode::LabelPositionBottom:
      Ax = leftX - 0.5 * textW;
      Ay = topY;
      Bx = rightX - 0.5 * textW;
      By = topY;
      break;
    default:
      // Default: simple connector only
      points->SetPoint(0, label.DisplayPosition[0], label.DisplayPosition[1], 0.0);
      points->SetPoint(1, label.AssignedPosition[0], label.AssignedPosition[1], 0.0);
      points->SetPoint(2, label.DisplayPosition[0], label.DisplayPosition[1], 0.0);
      points->SetPoint(3, label.AssignedPosition[0], label.AssignedPosition[1], 0.0);
      points->Modified();
      label.LinePolyData->Modified();
      return;
  }

  double anchorX = label.DisplayPosition[0];
  double anchorY = label.DisplayPosition[1];
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

  // Fallback: if connector is <3px, route to text box center for visibility
  double centerX = (leftX + rightX) * 0.5;
  double centerY = (bottomY + topY) * 0.5;
  double dx = projX - anchorX;
  double dy = projY - anchorY;
  double dist2 = dx*dx + dy*dy;
  if (dist2 < 9.0)
  {
    projX = centerX;
    projY = centerY;
  }

  points->SetPoint(0, Ax, Ay, 0.0);
  points->SetPoint(1, Bx, By, 0.0);
  points->SetPoint(2, anchorX, anchorY, 0.0);
  points->SetPoint(3, projX, projY, 0.0);
  points->Modified();
  label.LinePolyData->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::AdjustLabelsForCollision()
{
  if (!this->Renderer)
  {
    return;
  }

  int* viewportSize = this->Renderer->GetSize();
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
      bool ok = false;
      if (this->DisplayableManager.GetPointer())
      {
        ok = this->DisplayableManager.GetPointer()->GetLabelInfo(kv.second.DisplayNode, kv.second.LabelIndex, bi);
      }
      else
      {
        // Fallback if no displayable manager is set
        ok = kv.second.DisplayNode->GetLabelInfo(kv.second.LabelIndex, bi);
      }
      if (ok && bi.LabelPosition == side && kv.second.TextActor->GetVisibility())
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
      std::sort(group.begin(), group.end(), [](LabelInfo* a, LabelInfo* b)
        { return a->AssignedPosition[1] < b->AssignedPosition[1]; });
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
      std::sort(group.begin(), group.end(), [](LabelInfo* a, LabelInfo* b)
        { return a->AssignedPosition[0] < b->AssignedPosition[0]; });
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
}

//----------------------------------------------------------------------------
bool vtkMRMLLabelsWidgetRepresentation::CheckCollision(const double pos1[2], const double size1[2],
                                                       const double pos2[2], const double size2[2])
{
  bool xCollision = (pos1[0] < pos2[0] + size2[0]) && (pos1[0] + size1[0] > pos2[0]);
  bool yCollision = (pos1[1] < pos2[1] + size2[1]) && (pos1[1] + size1[1] > pos2[1]);
  return xCollision && yCollision;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::GetActors(vtkPropCollection* pc)
{
  for (auto& it : this->Labels)
  {
    if (it.second.TextActor)
    {
      it.second.TextActor->GetActors(pc);
    }
    if (it.second.LineActor)
    {
      it.second.LineActor->GetActors(pc);
    }
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation::ReleaseGraphicsResources(vtkWindow* window)
{
  for (auto& it : this->Labels)
  {
    if (it.second.TextActor)
    {
      it.second.TextActor->ReleaseGraphicsResources(window);
    }
    if (it.second.LineActor)
    {
      it.second.LineActor->ReleaseGraphicsResources(window);
    }
  }
}

//----------------------------------------------------------------------------
int vtkMRMLLabelsWidgetRepresentation::RenderOverlay(vtkViewport* viewport)
{
  int count = 0;
  for (auto& it : this->Labels)
  {
    if (it.second.TextActor && it.second.TextActor->GetVisibility())
    {
      count += it.second.TextActor->RenderOverlay(viewport);
    }
    if (it.second.LineActor && it.second.LineActor->GetVisibility())
    {
      count += it.second.LineActor->RenderOverlay(viewport);
    }
  }
  return count;
}

//----------------------------------------------------------------------------
int vtkMRMLLabelsWidgetRepresentation::RenderOpaqueGeometry(vtkViewport* viewport)
{
  int count = 0;
  for (auto& it : this->Labels)
  {
    if (it.second.TextActor && it.second.TextActor->GetVisibility())
    {
      count += it.second.TextActor->RenderOpaqueGeometry(viewport);
    }
    if (it.second.LineActor && it.second.LineActor->GetVisibility())
    {
      count += it.second.LineActor->RenderOpaqueGeometry(viewport);
    }
  }
  return count;
}

//----------------------------------------------------------------------------
int vtkMRMLLabelsWidgetRepresentation::RenderTranslucentPolygonalGeometry(vtkViewport* viewport)
{
  int count = 0;
  for (auto& it : this->Labels)
  {
    if (it.second.TextActor && it.second.TextActor->GetVisibility())
    {
      count += it.second.TextActor->RenderTranslucentPolygonalGeometry(viewport);
    }
    if (it.second.LineActor && it.second.LineActor->GetVisibility())
    {
      count += it.second.LineActor->RenderTranslucentPolygonalGeometry(viewport);
    }
  }
  return count;
}

//----------------------------------------------------------------------------
vtkTypeBool vtkMRMLLabelsWidgetRepresentation::HasTranslucentPolygonalGeometry()
{
  for (auto& it : this->Labels)
  {
    if (it.second.TextActor && it.second.TextActor->GetVisibility()
      && it.second.TextActor->HasTranslucentPolygonalGeometry())
    {
      return true;
    }
    if (it.second.LineActor && it.second.LineActor->GetVisibility()
      && it.second.LineActor->HasTranslucentPolygonalGeometry())
    {
      return true;
    }
  }
  return false;
}
