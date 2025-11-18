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

#include "vtkMRMLLabelDisplayNode.h"

// MRML includes
#include "vtkMRMLDisplayableNode.h"
#include "vtkMRMLScene.h"

// VTK includes
#include <vtkCommand.h>
#include <vtkObjectFactory.h>
#include <vtkTextProperty.h>

// STL includes
#include <sstream>
#include <string>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLLabelDisplayNode);

//----------------------------------------------------------------------------
const char* vtkMRMLLabelDisplayNode::TargetNodeReferenceRole = "targetNode";
const char* vtkMRMLLabelDisplayNode::TargetNodeReferenceMRMLAttributeName = "targetNodeRef";

//----------------------------------------------------------------------------
vtkMRMLLabelDisplayNode::vtkMRMLLabelDisplayNode()
{
}

//----------------------------------------------------------------------------
vtkMRMLLabelDisplayNode::~vtkMRMLLabelDisplayNode()
{
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::WriteXML(ostream& of, int nIndent)
{
  Superclass::WriteXML(of, nIndent);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::ReadXMLAttributes(const char** atts)
{
  int disabledModify = this->StartModify();
  Superclass::ReadXMLAttributes(atts);
  this->EndModify(disabledModify);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::CopyContent(vtkMRMLNode* anode, bool deepCopy/*=true*/)
{
  MRMLNodeModifyBlocker blocker(this);
  Superclass::CopyContent(anode, deepCopy);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::SetAndObserveTargetNodeID(const char* targetNodeID)
{
  this->SetAndObserveNodeReferenceID(this->GetTargetNodeReferenceRole(), targetNodeID);
}

//----------------------------------------------------------------------------
const char* vtkMRMLLabelDisplayNode::GetTargetNodeID()
{
  return this->GetNodeReferenceID(this->GetTargetNodeReferenceRole());
}

//----------------------------------------------------------------------------
vtkMRMLDisplayableNode* vtkMRMLLabelDisplayNode::GetTargetNode()
{
  return vtkMRMLDisplayableNode::SafeDownCast(
    this->GetNodeReference(this->GetTargetNodeReferenceRole()));
}

//----------------------------------------------------------------------------
const char* vtkMRMLLabelDisplayNode::GetTargetNodeReferenceRole()
{
  return vtkMRMLLabelDisplayNode::TargetNodeReferenceRole;
}

//----------------------------------------------------------------------------
const char* vtkMRMLLabelDisplayNode::GetTargetNodeReferenceMRMLAttributeName()
{
  return vtkMRMLLabelDisplayNode::TargetNodeReferenceMRMLAttributeName;
}

//----------------------------------------------------------------------------
int vtkMRMLLabelDisplayNode::GetNumberOfLabels()
{
  // Default implementation exposes a single label
  return 1;
}

//----------------------------------------------------------------------------
bool vtkMRMLLabelDisplayNode::GetLabelInfo(int labelIndex, LabelInfo& info)
{
  if (labelIndex != 0)
  {
    return false;
  }

  // Default implementation returns a simple label with default properties
  vtkMRMLDisplayableNode* targetNode = this->GetTargetNode();

  info.Text = std::string("Label");
  info.LabelID = std::string("0");

  // Anchor at target node center if available
  if (targetNode)
  {
    double bounds[6] = {0, 0, 0, 0, 0, 0};
    targetNode->GetRASBounds(bounds);
    info.AnchorPosition[0] = (bounds[0] + bounds[1]) / 2.0;
    info.AnchorPosition[1] = (bounds[2] + bounds[3]) / 2.0;
    info.AnchorPosition[2] = (bounds[4] + bounds[5]) / 2.0;
  }
  else
  {
    info.AnchorPosition[0] = info.AnchorPosition[1] = info.AnchorPosition[2] = 0.0;
  }

  // Default color from base display node
  double color[3] = { 1.0, 1.0, 1.0 };
  this->GetColor(color);
  info.Color[0] = color[0];
  info.Color[1] = color[1];
  info.Color[2] = color[2];

  info.Visible = (this->GetVisibility() != 0) && (targetNode != nullptr);
  info.LineVisible = true;
  // Apply override if available
  int overriddenPos = -1;
  if (this->GetLabelPositionOverrideForLabelID(info.LabelID.c_str(), overriddenPos))
  {
    info.LabelPosition = overriddenPos;
  }
  else
  {
    info.LabelPosition = LabelPositionDefault;
  }
  info.TextScale = 1.0;
  info.TextPropertyPtr = nullptr;

  return true;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::SetLabelPositionForLabelID(const char* labelID, int labelPosition)
{
  if (!labelID)
  {
    return;
  }
  this->LabelPositionOverrides[std::string(labelID)] = labelPosition;
  this->Modified();
}

//----------------------------------------------------------------------------
bool vtkMRMLLabelDisplayNode::GetLabelPositionOverrideForLabelID(const char* labelID, int& labelPosition) const
{
  if (!labelID)
  {
    return false;
  }
  auto it = this->LabelPositionOverrides.find(std::string(labelID));
  if (it == this->LabelPositionOverrides.end())
  {
    return false;
  }
  labelPosition = it->second;
  return true;
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::ClearLabelPositionForLabelID(const char* labelID)
{
  if (!labelID)
  {
    return;
  }
  auto it = this->LabelPositionOverrides.find(std::string(labelID));
  if (it != this->LabelPositionOverrides.end())
  {
    this->LabelPositionOverrides.erase(it);
    this->Modified();
  }
}

//----------------------------------------------------------------------------
void vtkMRMLLabelDisplayNode::ClearAllLabelPositionOverrides()
{
  if (!this->LabelPositionOverrides.empty())
  {
    this->LabelPositionOverrides.clear();
    this->Modified();
  }
}

//----------------------------------------------------------------------------
bool vtkMRMLLabelDisplayNode::SetLabelPositionForLabelIndex(int labelIndex, int labelPosition)
{
  LabelInfo info;
  if (!this->GetLabelInfo(labelIndex, info))
  {
    return false;
  }
  if (info.LabelID.empty())
  {
    return false;
  }
  this->SetLabelPositionForLabelID(info.LabelID.c_str(), labelPosition);
  return true;
}

//----------------------------------------------------------------------------
bool vtkMRMLLabelDisplayNode::GetLabelPositionOverrideForLabelIndex(int labelIndex, int& labelPosition)
{
  LabelInfo info;
  if (!this->GetLabelInfo(labelIndex, info))
  {
    return false;
  }
  if (info.LabelID.empty())
  {
    return false;
  }
  return this->GetLabelPositionOverrideForLabelID(info.LabelID.c_str(), labelPosition);
}
