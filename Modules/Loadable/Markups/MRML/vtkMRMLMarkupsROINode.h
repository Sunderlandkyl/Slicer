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

class vtkImplicitFunction;

/// \brief MRML node to represent an ROI markup
///
/// Coordinate systems used:
///   - Local:
///   - ROI:
///   - World: Patient coordinate system (RAS)
///
/// TODO: Finish coordinate system description. Control points are defined in local coordinates, but ROI is calculated in World
/// (to account for non-linear transform?). This may present a problem as control points should not be misaligned.
///
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


  /// Set of the Nth control point position from coordinates
  /// \sa SetNthControlPointPositionFromPointer, SetNthControlPointPositionFromArray
  void SetNthControlPointPosition(const int pointIndex,
    const double x, const double y, const double z, int positionStatus = vtkMRMLMarkupsNode::PositionDefined) override;
  /// Set of the Nth control point position and orientation from an array using World coordinate system.
  /// \sa SetNthControlPointPosition
  void SetNthControlPointPositionOrientationWorldFromArray(const int pointIndex,
    const double pos[3], const double orientationMatrix[9], const char* associatedNodeID, int positionStatus = vtkMRMLMarkupsNode::PositionDefined) override;

  ///
  virtual void UpdateControlPointsFromROI();
  virtual void UpdateControlPointsFromBoxROI();

  ///
  virtual void UpdateROIFromControlPoints();
  virtual void UpdateBoxROIFromControlPoints();
  virtual void UpdateSphereROIFromControlPoints();

  enum
  {
    Box,
    Sphere,
    Ellipsoid,
    Ultrasound,
  };

  enum
  {
    Corner,
    BoundingBox,
  };

  /// The origin of the ROI
  /// Calculated as the location of the 0th markup point
  //void GetOrigin(double origin[3]);
  //void SetOrigin(const double origin[3]);
  //virtual const double* GetOrigin() VTK_SIZEHINT(3);
  //virtual void GetOrigin(double& x, double& y, double& z);
  //virtual void GetOrigin(double origin[3]);

  vtkGetVector3Macro(Origin, double);
  vtkGetVector3Macro(XAxis, double);
  vtkGetVector3Macro(YAxis, double);
  vtkGetVector3Macro(ZAxis, double);
  vtkGetVector3Macro(SideLengths, double);
  vtkGetVector6Macro(Bounds, double);

  void GetOriginWorld(double origin[3]);
  void SetOriginWorld(const double origin[3]);

  //void GetSideLengths(double lengths[3]);
  /*void GetDirection(int axis, double direction[3]);*/

  vtkGetMacro(ROIType, int);
  vtkSetMacro(ROIType, int);

  vtkGetMacro(AxisAligned, bool);
  vtkSetMacro(AxisAligned, bool);
  vtkBooleanMacro(AxisAligned, bool);

  /// Alternative method to propagate events generated in Display nodes
  void ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData) override;

protected:

  int ROIType;

  // These variables are defined in the AxisAligned "ROI" coordinate system
  double Origin[3];
  double SideLengths[3];
  double Bounds[6];

  // TODO: Replace with 4x4 matrix
  // Can leave accesors to get individual axis from the matrix.
  double XAxis[3];
  double YAxis[3];
  double ZAxis[3];

  bool AxisAligned;
  bool IsUpdatingControlPoints;
  vtkMTimeType ROIUpdatedTime;

  vtkImplicitFunction* ROIFunction;

  vtkMRMLMarkupsROINode();
  ~vtkMRMLMarkupsROINode() override;
  vtkMRMLMarkupsROINode(const vtkMRMLMarkupsROINode&);
  void operator=(const vtkMRMLMarkupsROINode&);
};

#endif
