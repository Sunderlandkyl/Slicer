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
#include <vtkMatrix4x4.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTransform.h>

// std includes
#include <vector>

class vtkImplicitFunction;
class vtkPlanes;

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

  /// TODO
  vtkGetVector3Macro(SideLengths, double);
  void SetSideLengths(const double sideLengths[3]);
  void SetSideLengths(double x, double y, double z);

  void SetXYZ(double center[3]) { this->SetOriginWorld(center); };
  void SetXYZ(double x, double y, double z) { double tempXYZ[3] = { x, y, z }; this->SetXYZ(tempXYZ); };
  void SetRadiusXYZ(double radiusXYZ[3]) { this->SetSideLengths(radiusXYZ[0] * 2, radiusXYZ[1] * 2, radiusXYZ[2] * 2); };
  void SetRadiusXYZ(double x, double y, double z) { double tempXYZ[3] = { x, y, z }; this->SetRadiusXYZ(tempXYZ); };

  void GetXYZ(double center[3]) { this->GetOriginWorld(center); };
  void GetRadiusXYZ(double radiusXYZ[3])
  {
    double tempSideLengths[3] = { 0.0, 0.0, 0.0 };
    this->GetSideLengths(tempSideLengths);
    radiusXYZ[0] = 2 * tempSideLengths[0];
    radiusXYZ[1] = 2 * tempSideLengths[1];
    radiusXYZ[2] = 2 * tempSideLengths[2];
  };

  void GetTransformedPlanes(vtkPlanes* planes);

  /// The origin of the ROI
  void GetOrigin(double origin[3]);
  void GetOriginWorld(double origin[3]);
  void SetOriginWorld(const double origin[3]);
  void SetOrigin(const double origin[3]);

  void GetXAxisWorld(double axis_World[3]);
  void GetYAxisWorld(double axis_World[3]);
  void GetZAxisWorld(double axis_World[3]);
  void GetAxisWorld(int axisIndex, double axis_World[3]);

  void GetXAxisLocal(double axis_Local[3]);
  void GetYAxisLocal(double axis_Local[3]);
  void GetZAxisLocal(double axis_Local[3]);
  void GetAxisLocal(int axisIndex, double axis_Local[3]);

  vtkGetObjectMacro(ROIToLocalMatrix, vtkMatrix4x4);

  vtkGetMacro(ROIType, int);
  void SetROIType(int roiType);

  const char* GetROITypeAsString(int roiType);
  int GetROITypeFromString(const char* roiType);

  ///
  virtual void UpdateROIFromControlPoints();
  virtual void UpdateBoxROIFromControlPoints();
  virtual void UpdateBoundingBoxROIFromControlPoints();

  ///
  virtual void UpdateControlPointsFromROI(int positionStatus = vtkMRMLMarkupsNode::PositionDefined);
  virtual void UpdateControlPointsFromBoundingBoxROI(int positionStatus = vtkMRMLMarkupsNode::PositionDefined);

  enum
    {
    // Separate class
    BOX,
    BOUNDING_BOX,

    // Separate class
    //SPHERE,
    //ELLIPSOID,

    // Separate class
    /*CURVED_BOX,*/
    ROI_TYPE_LAST
    };

  // Scale handle indexes
  enum
    {
    L_FACE_POINT,
    R_FACE_POINT,
    P_FACE_POINT,
    A_FACE_POINT,
    I_FACE_POINT,
    S_FACE_POINT,

    LAI_CORNER_POINT,
    RAI_CORNER_POINT,
    LPI_CORNER_POINT,
    RPI_CORNER_POINT,
    LAS_CORNER_POINT,
    RAS_CORNER_POINT,
    LPS_CORNER_POINT,
    RPS_CORNER_POINT,
    };

  void OnTransformNodeReferenceChanged(vtkMRMLTransformNode* transformNode) override;

  /// Alternative method to propagate events generated in Display nodes
  void ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData) override;

  void UpdateInteractionHandleToWorldMatrix() override;

  void GetBoundsROI(double bounds[6]);

  /// Create default storage node or nullptr if does not have one
  vtkMRMLStorageNode* CreateDefaultStorageNode() override;

  vtkSetMacro(InsideOut, bool);
  vtkGetMacro(InsideOut, bool);
  vtkBooleanMacro(InsideOut, bool);

protected:

  bool InsideOut;

  int ROIType;

  // These variables are defined in the axis aligned "ROI" coordinate system
  double SideLengths[3];

  // TODO: Replace with 4x4 matrix
  // Can leave accesors to get individual axis from the matrix.
  vtkSmartPointer<vtkMatrix4x4> ROIToLocalMatrix;

  bool IsUpdatingControlPointsFromROI;
  bool IsUpdatingROIFromControlPoints;
  vtkMTimeType ROIUpdatedTime;

  vtkMRMLMarkupsROINode();
  ~vtkMRMLMarkupsROINode() override;
  vtkMRMLMarkupsROINode(const vtkMRMLMarkupsROINode&);
  void operator=(const vtkMRMLMarkupsROINode&);
};

#endif
