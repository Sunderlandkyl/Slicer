/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

#ifndef __vtkMRMLSelectionDisplayNode_h
#define __vtkMRMLSelectionDisplayNode_h

// MRML includes
#include "vtkMRMLDisplayNode.h"

/// \brief MRML node to store display properties of slice nodes.
///
/// This node controls appearance of slice intersections in slice views
/// and slices in 3D views.
class VTK_MRML_EXPORT vtkMRMLSelectionDisplayNode : public vtkMRMLDisplayNode
{
public:
  static vtkMRMLSelectionDisplayNode *New();
  vtkTypeMacro(vtkMRMLSelectionDisplayNode,vtkMRMLDisplayNode);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Read node attributes from XML file
  void ReadXMLAttributes(const char** atts) override;

  /// Write this node's information to a MRML file in XML format.
  void WriteXML(ostream& of, int indent) override;

  /// Copy node content (excludes basic data, such as name and node references).
  /// \sa vtkMRMLNode::CopyContent
  vtkMRMLCopyContentMacro(vtkMRMLSelectionDisplayNode);

  vtkMRMLNode* CreateNodeInstance() override;

  /// Get node XML tag name (like Volume, Model)
  const char* GetNodeTagName() override {return "SelectionDisplay";}

  vtkSetMacro(HighlightStrength, double);
  vtkGetMacro(HighlightStrength, double);

  vtkSetVector3Macro(HighlightColor, double);
  vtkGetVector3Macro(HighlightColor, double);

protected:
  vtkMRMLSelectionDisplayNode();
  ~vtkMRMLSelectionDisplayNode() override;
  vtkMRMLSelectionDisplayNode(const vtkMRMLSelectionDisplayNode&);
  void operator=(const vtkMRMLSelectionDisplayNode&);

  double HighlightStrength{ 30.0 };
  double HighlightColor[3]{ 1.0, 1.0, 1.0 };
};

#endif
