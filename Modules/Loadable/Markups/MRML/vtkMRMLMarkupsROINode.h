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

#ifndef __vtkMRMLMarkupsROINode_h
#define __vtkMRMLMarkupsROINode_h

// MRML includes
#include "vtkMRMLDisplayableNode.h"
#include "vtkMRMLModelNode.h"

// Markups includes
#include "vtkSlicerMarkupsModuleMRMLExport.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsNode.h"

// VTK includes
#include <vtkStringArray.h>

// std includes
#include <vector>

/// \brief MRML node to represent a curve markup
/// Curve Markups nodes contain N control points.
/// Visualization parameters are set in the vtkMRMLMarkupsDisplayNode class.
///
/// Markups is intended to be used for manual marking/editing of point positions.
///
/// Coordinate systems used:
///   - Local: Current node's coordinate system where the position of the control and curve points are defined.
///            Local coordinates can be converted to world by concatenating all parent transforms on the current node.
///   - Surface: Model node's coordinate system where the polydata used for ShortestDistanceOnSurface pathfinding is defined.
///            Surface coordinates can be converted to world by concatenating all parent transforms on the surface node.
///   - World: Patient coordinate system (RAS)
/// \ingroup Slicer_QtModules_Markups
class  VTK_SLICER_MARKUPS_MODULE_MRML_EXPORT vtkMRMLMarkupsROINode : public vtkMRMLMarkupsNode
{
public:
  static vtkMRMLMarkupsROINode *New();
  vtkTypeMacro(vtkMRMLMarkupsROINode,vtkMRMLMarkupsNode);
  /// Print out the node information to the output stream
  void PrintSelf(ostream& os, vtkIndent indent) override;

  const char* GetIcon() override {return ":/Icons/MarkupsCurveMouseModePlace.png";}

  //--------------------------------------------------------------------------
  // MRMLNode methods
  //--------------------------------------------------------------------------

  vtkMRMLNode* CreateNodeInstance() override;
  /// Get node XML tag name (like Volume, Model)
  const char* GetNodeTagName() override {return "MarkupsROI";}

  /// Read node attributes from XML file
  void ReadXMLAttributes( const char** atts) override;

  /// Write this node's information to a MRML file in XML format.
  void WriteXML(ostream& of, int indent) override;

  /// Copy node content (excludes basic data, such as name and node references).
  /// \sa vtkMRMLNode::CopyContent
  vtkMRMLCopyContentMacro(vtkMRMLMarkupsROINode);

  /// The origin of the ROI
  /// Calculated as the location of the 0th markup point
  void GetOrigin(double origin[3]);
  void SetOrigin(const double origin[3]);
  void GetOriginWorld(double origin[3]);
  void SetOriginWorld(const double origin[3]);

  ///
  virtual void UpdateBoxROIControlPoints();

  /// Set of the Nth control point position from coordinates
  /// \sa SetNthControlPointPositionFromPointer, SetNthControlPointPositionFromArray
  void SetNthControlPointPosition(const int pointIndex,
    const double x, const double y, const double z, int positionStatus = vtkMRMLMarkupsNode::PositionDefined) override;
  /// Set of the Nth control point position and orientation from an array using World coordinate system.
  /// \sa SetNthControlPointPosition
  void SetNthControlPointPositionOrientationWorldFromArray(const int pointIndex,
    const double pos[3], const double orientationMatrix[9], const char* associatedNodeID, int positionStatus = vtkMRMLMarkupsNode::PositionDefined) override;

protected:

  vtkMRMLMarkupsROINode();
  ~vtkMRMLMarkupsROINode() override;
  vtkMRMLMarkupsROINode(const vtkMRMLMarkupsROINode&);
  void operator=(const vtkMRMLMarkupsROINode&);
};

#endif
