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

#include "vtkMRMLLabelsWidgetRepresentation3D.h"

// MRML includes
#include <vtkMRMLLabelDisplayNode.h>

// VTK includes
#include <vtkCoordinate.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkRenderer.h>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLLabelsWidgetRepresentation3D);

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation3D::vtkMRMLLabelsWidgetRepresentation3D()
{
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation3D::~vtkMRMLLabelsWidgetRepresentation3D()
{
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation3D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation3D::WorldToDisplay(const double worldPos[3], double displayPos[2])
{
  if (!this->Renderer)
  {
    displayPos[0] = 0;
    displayPos[1] = 0;
    return;
  }

  // Use VTK coordinate conversion
  vtkNew<vtkCoordinate> coordinate;
  coordinate->SetCoordinateSystemToWorld();
  coordinate->SetValue(worldPos[0], worldPos[1], worldPos[2]);
  int* display = coordinate->GetComputedDisplayValue(this->Renderer);
  displayPos[0] = display[0];
  displayPos[1] = display[1];
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidgetRepresentation3D::UpdateLabelPositions()
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
    // Get cached text size (compute lazily if dirty)
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

  // Collision avoidance after initial assignments
  this->AdjustLabelsForCollision();

  // Update actors
  this->UpdateLabelActors();
}
