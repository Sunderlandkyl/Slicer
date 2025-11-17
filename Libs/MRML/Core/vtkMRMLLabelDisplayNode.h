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

/// .NAME vtkMRMLLabelDisplayNode - MRML node to represent a label attached to any displayable node
/// .SECTION Description
/// This display node allows attaching text labels to any MRML node at a specified anchor position.
/// The label can be positioned at different locations (left, right, top, bottom) in the view
/// with automatic collision avoidance.
///

#ifndef __vtkMRMLLabelDisplayNode_h
#define __vtkMRMLLabelDisplayNode_h

#include "vtkMRMLDisplayNode.h"

// VTK includes
#include <vtkTextProperty.h>

// STD includes
#include <string>
#include <map>

/// \brief MRML node to represent a label attached to any displayable node
class VTK_MRML_EXPORT vtkMRMLLabelDisplayNode : public vtkMRMLDisplayNode
{
public:
  static vtkMRMLLabelDisplayNode* New();
  vtkTypeMacro(vtkMRMLLabelDisplayNode, vtkMRMLDisplayNode);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  vtkMRMLNode* CreateNodeInstance() override;

  /// Read node attributes from XML (MRML) file
  void ReadXMLAttributes(const char** atts) override;

  /// Write this node's information to a MRML file in XML format.
  void WriteXML(ostream& of, int indent) override;

  /// Copy node content (excludes basic data, such as name and node references).
  vtkMRMLCopyContentMacro(vtkMRMLLabelDisplayNode);

  /// Get node XML tag name
  const char* GetNodeTagName() override { return "NodeLabelDisplay"; };


  /// Anchor position calculation modes
  enum AnchorPositionModeType
  {
    AnchorPositionManual = 0,           ///< User specifies anchor position manually
    AnchorPositionNodeCenter,           ///< Use the center of the node's bounding box
    AnchorPositionSegmentLargestIsland, ///< Use center of mass of largest segment island on slice (for segmentation nodes)
    AnchorPositionCustom,               ///< Custom calculation defined by subclasses
    AnchorPositionMode_Last
  };

  /// Label position constants for use in LabelInfo
  enum LabelPositionType
  {
    LabelPositionDefault = 0, ///< Default position (at anchor point)
    LabelPositionLeft,        ///< Left edge of view
    LabelPositionRight,       ///< Right edge of view
    LabelPositionTop,         ///< Top edge of view
    LabelPositionBottom,      ///< Bottom edge of view
    LabelPosition_Last
  };

  /// Set the target displayable node ID
  void SetAndObserveTargetNodeID(const char* targetNodeID);

  /// Get the target displayable node ID
  const char* GetTargetNodeID();

  /// Get the target displayable node
  vtkMRMLDisplayableNode* GetTargetNode();

  // Multi-label API: Base contract for subclasses to provide one or more labels per target node
  struct LabelInfo
  {
    std::string Text;          ///< Text content of label
    std::string LabelID;       ///< Stable identifier for this label (e.g., SegmentID, ControlPointID)
    double AnchorPosition[3] {0.0, 0.0, 0.0}; ///< World coordinates of anchor point
    double Color[3] {1.0, 1.0, 0.0};          ///< RGB color of the label/line
    bool Visible { true };     ///< Overall visibility of this label
    bool LineVisible { true }; ///< Whether the connecting line is visible
    int LabelPosition { LabelPositionDefault }; ///< Placement preference in view
    double TextScale { 3.0 };  ///< Relative text scale
    vtkTextProperty* TextPropertyPtr { nullptr }; ///< Optional per-label text property (non-owning)
  };

  /// Return number of labels exposed by this display node (default: 1)
  virtual int GetNumberOfLabels();

  /// Populate information for a given label index. Returns true on success.
  /// Default implementation exposes a single label using default properties.
  virtual bool GetLabelInfo(int labelIndex, LabelInfo& info);

  //@{
  /// Configure per-label position using a stable LabelID (for example, SegmentID or ControlPointID).
  /// If a position is set for a LabelID then it overrides the LabelPosition provided by subclasses in GetLabelInfo.
  void SetLabelPositionForLabelID(const char* labelID, int labelPosition);
  /// Returns true and sets labelPosition if there is an override for the specified LabelID; otherwise returns false.
  bool GetLabelPositionOverrideForLabelID(const char* labelID, int& labelPosition) const;
  /// Remove override for a specific LabelID
  void ClearLabelPositionForLabelID(const char* labelID);
  /// Remove all per-label position overrides
  void ClearAllLabelPositionOverrides();

  /// Convenience helpers to set/get override by label index (resolved to LabelID internally).
  bool SetLabelPositionForLabelIndex(int labelIndex, int labelPosition);
  bool GetLabelPositionOverrideForLabelIndex(int labelIndex, int& labelPosition);
  //@}

  /// Events
  enum
  {
    LabelTextModifiedEvent = 29000,
    AnchorPositionModifiedEvent,
    LabelPropertiesModifiedEvent
  };

protected:
  vtkMRMLLabelDisplayNode();
  ~vtkMRMLLabelDisplayNode() override;
  vtkMRMLLabelDisplayNode(const vtkMRMLLabelDisplayNode&);
  void operator=(const vtkMRMLLabelDisplayNode&);

  static const char* TargetNodeReferenceRole;
  static const char* TargetNodeReferenceMRMLAttributeName;

  virtual const char* GetTargetNodeReferenceRole();
  virtual const char* GetTargetNodeReferenceMRMLAttributeName();

  /// Per-label position overrides keyed by LabelID
  std::map<std::string, int> LabelPositionOverrides;
};

#endif
