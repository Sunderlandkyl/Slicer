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

#include "vtkMRMLLabelsWidgetRepresentation2D.h"

// MRML includes
#include <vtkMRMLLabelDisplayNode.h>
#include <vtkMRMLSliceNode.h>

// VTK includes
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkRenderer.h>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLLabelsWidgetRepresentation2D);

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation2D::vtkMRMLLabelsWidgetRepresentation2D()
{
  this->SliceNode = nullptr;
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation2D::~vtkMRMLLabelsWidgetRepresentation2D()
{
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation2D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation2D::SetSliceNode(vtkMRMLSliceNode* sliceNode)
{
  if (this->SliceNode == sliceNode)
  {
    return;
  }
  this->SliceNode = sliceNode;
  this->Modified();
}

//----------------------------------------------------------------------------
vtkMRMLSliceNode* vtkMRMLLabelsWidgetRepresentation2D::GetSliceNode()
{
  return this->SliceNode;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation2D::WorldToDisplay(const double worldPos[3], double displayPos[2])
{
  if (!this->SliceNode || !this->Renderer)
  {
    displayPos[0] = 0;
    displayPos[1] = 0;
    return;
  }

  // Compute slice XY coordinates directly (these are in the slice view's display coordinate system)
  vtkMatrix4x4* xyToRAS = this->SliceNode->GetXYToRAS();
  vtkNew<vtkMatrix4x4> rasToXY;
  vtkMatrix4x4::Invert(xyToRAS, rasToXY);

  double xyzw[4] = { worldPos[0], worldPos[1], worldPos[2], 1.0 };
  double xyPos[4] = {0,0,0,1};
  rasToXY->MultiplyPoint(xyzw, xyPos);

  // Use XY as display coordinates
  displayPos[0] = xyPos[0];
  displayPos[1] = xyPos[1];
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation2D::UpdateLabelPositions()
{
  if (!this->Renderer)
  {
    return;
  }

  // Update labels first to ensure we have the right number of label actors
  this->UpdateLabels();

  int* viewportSize = this->Renderer->GetSize();
  int margin = 10;

  // Update each label based on its position preference
  for (auto& it : this->Labels)
  {
    LabelInfo& info = it.second;
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
      info.TextActor->SetVisibility(false);
      if (info.LineActor)
      {
        info.LineActor->SetVisibility(false);
      }
      continue;
    }

    // Update anchor and display position (world->display conversion)
    info.AnchorPosition[0] = baseInfo.AnchorPosition[0];
    info.AnchorPosition[1] = baseInfo.AnchorPosition[1];
    info.AnchorPosition[2] = baseInfo.AnchorPosition[2];
    this->WorldToDisplay(info.AnchorPosition, info.DisplayPosition);

    // Cache viewport size changes
    if (this->Renderer)
    {
      int* vs = this->Renderer->GetSize();
      if (vs[0] != info.CachedViewportSize[0] || vs[1] != info.CachedViewportSize[1])
      {
        info.CachedViewportSize[0] = vs[0];
        info.CachedViewportSize[1] = vs[1];
        info.SizeDirty = true;
      }
    }

    int labelPosition = baseInfo.LabelPosition;
    if (info.SizeDirty)
    {
      double bbox[4] = { 0, 0, 0, 0 };
      info.TextActor->GetBoundingBox(this->Renderer, bbox);
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
        info.TextActor->GetTextProperty()->SetJustificationToLeft();
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

  // Collision avoidance pass
  this->AdjustLabelsForCollision();

  // Collision avoidance done; update actors
  this->UpdateLabelActors();
}
