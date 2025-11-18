/*==============================================================================

  Program: 3D Slicer

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#include "vtkMRMLSegmentationLabelDisplayNode.h"

// MRML includes
#include "vtkMRMLSegmentationNode.h"
#include "vtkMRMLSegmentationDisplayNode.h"
#include "vtkMRMLScene.h"

// VTK includes
#include <vtkObjectFactory.h>
#include <vtkTextProperty.h>
#include <vtkIntArray.h>

// Segmentation core includes
#include <vtkSegmentation.h>
#include <vtkSegment.h>

// STD includes
#include <vector>
#include <string>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLSegmentationLabelDisplayNode);

//----------------------------------------------------------------------------
vtkMRMLSegmentationLabelDisplayNode::vtkMRMLSegmentationLabelDisplayNode()
{
  this->TargetEvents = vtkSmartPointer<vtkIntArray>::New();
  this->TargetEvents->InsertNextValue(vtkCommand::ModifiedEvent);
  this->TargetEvents->InsertNextValue(vtkMRMLSegmentationNode::SegmentationChangedEvent);
}

//----------------------------------------------------------------------------
vtkMRMLSegmentationLabelDisplayNode::~vtkMRMLSegmentationLabelDisplayNode() = default;

//----------------------------------------------------------------------------
void vtkMRMLSegmentationLabelDisplayNode::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
std::vector<std::string> vtkMRMLSegmentationLabelDisplayNode::GetSegmentIDs()
{
  std::vector<std::string> ids;
  vtkMRMLSegmentationNode* segNode = vtkMRMLSegmentationNode::SafeDownCast(this->GetTargetNode());
  if (!segNode)
  {
    return ids;
  }

  vtkMRMLDisplayNode* dn = segNode->GetDisplayNode();
  vtkMRMLSegmentationDisplayNode* segDisplay = vtkMRMLSegmentationDisplayNode::SafeDownCast(dn);
  if (segDisplay)
  {
    segDisplay->GetVisibleSegmentIDs(ids);
  }
  return ids;
}

//----------------------------------------------------------------------------
int vtkMRMLSegmentationLabelDisplayNode::GetNumberOfLabels()
{
  return static_cast<int>(this->GetSegmentIDs().size());
}

//----------------------------------------------------------------------------
bool vtkMRMLSegmentationLabelDisplayNode::GetLabelInfo(int labelIndex, LabelInfo& info)
{
  std::vector<std::string> ids = this->GetSegmentIDs();
  if (labelIndex < 0 || labelIndex >= static_cast<int>(ids.size()))
  {
    return false;
  }

  vtkMRMLSegmentationNode* segNode = vtkMRMLSegmentationNode::SafeDownCast(this->GetTargetNode());
  if (!segNode)
  {
    return false;
  }

  vtkMRMLSegmentationDisplayNode* segDisplay = vtkMRMLSegmentationDisplayNode::SafeDownCast(segNode->GetDisplayNode());

  const std::string& segmentID = ids[labelIndex];

  // Text: prefer segment name, fallback to ID
  if (segNode->GetSegmentation())
  {
    vtkSegment* segment = segNode->GetSegmentation()->GetSegment(segmentID);
    if (segment && segment->GetName())
    {
      info.Text = segment->GetName();
    }
    else
    {
      info.Text = segmentID;
    }
  }
  else
  {
    info.Text = segmentID;
  }
  // Stable label identifier
  info.LabelID = segmentID;

  // Anchor position: segment center in RAS
  double centerRAS[3] = {0.0, 0.0, 0.0};
  segNode->GetSegmentCenterRAS(segmentID, centerRAS);
  info.AnchorPosition[0] = centerRAS[0];
  info.AnchorPosition[1] = centerRAS[1];
  info.AnchorPosition[2] = centerRAS[2];

  // Color: prefer per-segment color from display node, otherwise fall back to this node's color
  double color[3] = {1.0, 1.0, 1.0};
  if (segDisplay && segDisplay->GetSegmentColor(segmentID, color))
  {
    info.Color[0] = color[0];
    info.Color[1] = color[1];
    info.Color[2] = color[2];
  }
  else
  {
    this->GetColor(color);
    info.Color[0] = color[0];
    info.Color[1] = color[1];
    info.Color[2] = color[2];
  }

  // Visibility: combine our visibility, segmentation display visibility, and per-segment visibility
  bool visible = (this->GetVisibility() != 0) && (this->GetTargetNode() != nullptr);
  if (segDisplay)
  {
    visible = visible && segDisplay->GetSegmentVisibility(segmentID);
  }
  info.Visible = visible;

  // Use default values for display properties
  info.LineVisible = true;
  // Apply override if available
  int overriddenPos = -1;
  if (this->GetLabelPositionOverrideForLabelID(info.LabelID.c_str(), overriddenPos))
  {
    info.LabelPosition = overriddenPos;
  }
  else
  {
    info.LabelPosition = vtkMRMLLabelDisplayNode::LabelPositionDefault;
  }
  info.TextScale = 1.0;
  info.TextPropertyPtr = nullptr;

  return true;
}

//----------------------------------------------------------------------------
vtkIntArray* vtkMRMLSegmentationLabelDisplayNode::GetNodeReferenceEvents(const char* referenceRole)
{
  if (referenceRole && strcmp(referenceRole, this->GetTargetNodeReferenceRole()) == 0)
  {
    return this->TargetEvents;
  }
  return nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLSegmentationLabelDisplayNode::ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData)
{
  if (caller == this->GetTargetNode())
  {
    // Any change in the target segmentation should refresh labels
    this->Modified();
  }
  this->Superclass::ProcessMRMLEvents(caller, event, callData);
}
