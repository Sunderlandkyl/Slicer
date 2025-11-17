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

#include "vtkMRMLMarkupsLabelDisplayNode.h"

// MRML includes
#include "vtkMRMLMarkupsNode.h"
#include "vtkMRMLScene.h"

// VTK includes
#include <vtkObjectFactory.h>
#include <vtkTextProperty.h>
#include <vtkIntArray.h>

// STD includes
#include <string>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsLabelDisplayNode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsLabelDisplayNode::vtkMRMLMarkupsLabelDisplayNode()
{
  this->TargetEvents = vtkSmartPointer<vtkIntArray>::New();
  // Basic node modification + markups-specific point changes
  this->TargetEvents->InsertNextValue(vtkCommand::ModifiedEvent);
  this->TargetEvents->InsertNextValue(vtkMRMLMarkupsNode::PointAddedEvent);
  this->TargetEvents->InsertNextValue(vtkMRMLMarkupsNode::PointRemovedEvent);
  this->TargetEvents->InsertNextValue(vtkMRMLMarkupsNode::PointModifiedEvent);
  this->TargetEvents->InsertNextValue(vtkMRMLMarkupsNode::LabelFormatModifiedEvent);
}

//----------------------------------------------------------------------------
vtkMRMLMarkupsLabelDisplayNode::~vtkMRMLMarkupsLabelDisplayNode() = default;

//----------------------------------------------------------------------------
void vtkMRMLMarkupsLabelDisplayNode::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
int vtkMRMLMarkupsLabelDisplayNode::GetNumberOfLabels()
{
  vtkMRMLMarkupsNode* markupsNode = vtkMRMLMarkupsNode::SafeDownCast(this->GetTargetNode());
  if (!markupsNode)
  {
    return 0;
  }
  return markupsNode->GetNumberOfControlPoints();
}

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsLabelDisplayNode::GetLabelInfo(int labelIndex, LabelInfo& info)
{
  vtkMRMLMarkupsNode* markupsNode = vtkMRMLMarkupsNode::SafeDownCast(this->GetTargetNode());
  if (!markupsNode)
  {
    return false;
  }
  if (labelIndex < 0 || labelIndex >= markupsNode->GetNumberOfControlPoints())
  {
    return false;
  }

  // Text
  info.Text = markupsNode->GetNthControlPointLabel(labelIndex);
  // Stable label identifier for per-label overrides
  info.LabelID = markupsNode->GetNthControlPointID(labelIndex);

  // Anchor position: control point position in world coordinates
  double posWorld[3] = {0.0, 0.0, 0.0};
  if (markupsNode->GetNthControlPointPositionWorld(labelIndex, posWorld))
  {
    info.AnchorPosition[0] = posWorld[0];
    info.AnchorPosition[1] = posWorld[1];
    info.AnchorPosition[2] = posWorld[2];
  }

  // Color: use this node's display color for all labels
  double color[3] = {1.0, 1.0, 1.0};
  this->GetColor(color);
  info.Color[0] = color[0];
  info.Color[1] = color[1];
  info.Color[2] = color[2];

  // Visibility: combine our visibility with per-control-point visibility and definition
  bool visible = (this->GetVisibility() != 0) && (this->GetTargetNode() != nullptr)
    && markupsNode->GetNthControlPointPositionVisibility(labelIndex);
  info.Visible = visible;

  // Use default values for display properties
  info.LineVisible = true;
  // Apply per-label position override if available
  int overriddenPos = -1;
  if (!info.LabelID.empty() && this->GetLabelPositionOverrideForLabelID(info.LabelID.c_str(), overriddenPos))
  {
    info.LabelPosition = overriddenPos;
  }
  else
  {
    info.LabelPosition = vtkMRMLLabelDisplayNode::LabelPositionDefault;
  }
  info.TextScale = 3.0;
  info.TextPropertyPtr = nullptr;

  return true;
}

//----------------------------------------------------------------------------
vtkIntArray* vtkMRMLMarkupsLabelDisplayNode::GetNodeReferenceEvents(const char* referenceRole)
{
  if (referenceRole && strcmp(referenceRole, this->GetTargetNodeReferenceRole()) == 0)
  {
    return this->TargetEvents;
  }
  // No additional default events for other roles
  return nullptr;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsLabelDisplayNode::ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData)
{
  if (caller == this->GetTargetNode())
  {
    // Any change in the markups points or labels should refresh labels
    this->Modified();
  }
  this->Superclass::ProcessMRMLEvents(caller, event, callData);
}
