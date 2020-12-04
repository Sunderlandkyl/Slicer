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

#include "vtkMRMLMarkupsROINode.h"

// MRML includes
#include "vtkCurveGenerator.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLTransformNode.h"
#include "vtkMRMLUnitNode.h"
#include "vtkSlicerDijkstraGraphGeodesicPath.h"

// VTK includes
#include <vtkArrayCalculator.h>
#include <vtkBoundingBox.h>
#include <vtkCallbackCommand.h>
#include <vtkCellLocator.h>
#include <vtkCleanPolyData.h>
#include <vtkCommand.h>
#include <vtkCutter.h>
#include <vtkDoubleArray.h>
#include <vtkFrenetSerretFrame.h>
#include <vtkGeneralTransform.h>
#include <vtkGenericCell.h>
#include <vtkLine.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkOBBTree.h>
#include <vtkObjectFactory.h>
#include <vtkPassThroughFilter.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkStringArray.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangleFilter.h>

// STD includes
#include <sstream>

#include <vtkPCAStatistics.h>
#include <vtkTable.h>
#include <vtkOBBTree.h>
#include <vtkTimeStamp.h>

#include <vtkAddonMathUtilities.h>

//const int NUMBER_OF_BOX_CONTROL_POINTS = 3; // 1 center; 8 corners; 6 faces
const int NUMBER_OF_BOX_CONTROL_POINTS = 15; // 1 center; 8 corners; 6 faces
const int POINT_RIGHT_MASK = 0x1;
const int POINT_ANTERIOR_MASK = 0x2;
const int POINT_SUPERIOR_MASK = 0x4;

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROINode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::vtkMRMLMarkupsROINode()
{
  // Set RequiredNumberOfControlPoints to a very high number to remain
  // in place mode after placing a curve point.
  this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;
  this->PropertiesLabelText = "";
  this->ROIType = BOX;
  //this->ROIType = Sphere;

  this->Origin[0] = 0.0;
  this->Origin[1] = 0.0;
  this->Origin[1] = 0.0;

  this->SideLengths[0] = 0.0;
  this->SideLengths[1] = 0.0;
  this->SideLengths[2] = 0.0;

  this->Bounds[0] = 0.0;
  this->Bounds[1] = -1.0;
  this->Bounds[2] = 0.0;
  this->Bounds[3] = -1.0;
  this->Bounds[4] = 0.0;
  this->Bounds[5] = -1.0;

  this->IsUpdatingControlPointsFromROI = false;
  this->IsUpdatingROIFromControlPoints = false;
  this->ROIUpdatedTime = 0;

  this->CurveInputPoly->GetPoints()->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
}

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::~vtkMRMLMarkupsROINode() = default;

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::WriteXML(ostream& of, int nIndent)
{
  Superclass::WriteXML(of,nIndent);

  vtkMRMLWriteXMLBeginMacro(of);
  vtkMRMLWriteXMLEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::ReadXMLAttributes(const char** atts)
{
  MRMLNodeModifyBlocker blocker(this);

  this->Superclass::ReadXMLAttributes(atts);

  vtkMRMLReadXMLBeginMacro(atts);
  vtkMRMLReadXMLEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::CopyContent(vtkMRMLNode* anode, bool deepCopy/*=true*/)
{
  MRMLNodeModifyBlocker blocker(this);
  Superclass::CopyContent(anode, deepCopy);

  vtkMRMLCopyBeginMacro(anode);
  vtkMRMLCopyEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os,indent);

  vtkMRMLPrintBeginMacro(os, indent);
  vtkMRMLPrintEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetXAxisWorld(double axis_World[3])
{
  this->GetAxisWorld(0, axis_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetYAxisWorld(double axis_World[3])
{
  this->GetAxisWorld(1, axis_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetZAxisWorld(double axis_World[3])
{
  this->GetAxisWorld(2, axis_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetAxisWorld(int axisIndex, double axis_World[3])
{
  if (!this->ROIToLocalMatrix)
    {
    vtkErrorMacro("Invalid ROIToLocalMatrix");
    return;
    }

  if (axisIndex < 0 || axisIndex >= 3)
    {
    vtkErrorMacro("Invalid axisIndex. Must be 0, 1, or 2.");
    return;
    }

  if (!axis_World)
    {
    vtkErrorMacro("Invalid axis_World");
    return;
    }

  for (int i = 0; i < 3; ++i)
    {
    axis_World[i] = this->ROIToLocalMatrix->GetElement(axisIndex, i);
    }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData)
{
  if (caller == this->CurveInputPoly->GetPoints() && event == vtkCommand::ModifiedEvent)
    {
    this->UpdateROIFromControlPoints();
    }
  Superclass::ProcessMRMLEvents(caller, event, callData);
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetControlPointPositionsWorld(vtkPoints* points_World)
{
  bool wasUpdatingFromControlPoints = this->IsUpdatingROIFromControlPoints;
  bool wasUpdatingFromROI = this->IsUpdatingControlPointsFromROI;

  this->IsUpdatingROIFromControlPoints = true;
  this->IsUpdatingControlPointsFromROI = true;

  Superclass::SetControlPointPositionsWorld(points_World);

  this->IsUpdatingROIFromControlPoints = wasUpdatingFromControlPoints;
  this->IsUpdatingControlPointsFromROI = wasUpdatingFromROI;

  this->UpdateROIFromControlPoints();
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPosition(const int pointIndex,
  const double x, const double y, const double z, int positionStatus/*=PositionDefined*/)
{
  //MRMLNodeModifyBlocker blocker(this); // This may be too heavy-handed. We won't be able to tell which point was changed using event.
  if (this->IsUpdatingROIFromControlPoints)
    {
    Superclass::SetNthControlPointPosition(pointIndex, x, y, z, positionStatus);
    }
  else
    {
    this->UpdateROIFromControlPoints(pointIndex, x, y, z, positionStatus);
    }
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPositionOrientationWorldFromArray(const int pointIndex,
  const double pos[3], const double orientationMatrix[9], const char* associatedNodeID, int positionStatus/*=PositionDefined*/)
{
  //MRMLNodeModifyBlocker blocker(this); // This may be too heavy-handed. We won't be able to tell which point was changed using event.
  if (this->IsUpdatingROIFromControlPoints)
    {
    Superclass::SetNthControlPointPositionOrientationWorldFromArray(pointIndex,
      pos, orientationMatrix, associatedNodeID, positionStatus);
    }
  else
    {
    // TODO orientation, associated node?
    this->UpdateROIFromControlPoints(pointIndex, pos[0], pos[1], pos[2], positionStatus);
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetROIType(int roiType)
{
  if (this->ROIType == roiType)
    {
    return;
    }

  this->ROIType = roiType;
  switch (roiType)
    {
    case BOX:
      this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;
      break;
    // TODO: Other representations, including boounding-box
    default:
      break;
    }
  this->UpdateROIFromControlPoints();
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetOriginWorld(double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("GetOriginWorld: Invalid origin argument");
    return;
    }

  this->GetOrigin(origin_World);
  this->TransformPointToWorld(origin_World, origin_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetOriginWorld(const double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("SetOriginWorld: Invalid origin argument");
    return;
    }

  this->Origin[0] = origin_World[0];
  this->Origin[1] = origin_World[1];
  this->Origin[2] = origin_World[2];
  //for (int i = 0; i < 3; ++i)
  //  {
  //  this->ROIToLocalMatrix->SetElement(3, i, origin_World[i]);
  //  }
  this->UpdateControlPointsFromROI();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromROI(int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  if (this->IsUpdatingControlPointsFromROI)
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(this);
  this->IsUpdatingControlPointsFromROI = true;

  switch (this->ROIType)
    {
    case BOX:
      this->UpdateControlPointsFromBoxROI(positionStatus);
      break;
    default:
      vtkErrorMacro("vtkMRMLMarkupsROINode: Unknown ROI type")
      break;
    }

  this->IsUpdatingControlPointsFromROI = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromBoxROI(int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  int numberOfPoints = this->RequiredNumberOfControlPoints;
  if (this->SideLengths[0] <= 0.0 && this->SideLengths[1] <= 0.0 && this->SideLengths[2] <= 0.0)
    {
    numberOfPoints = 1;
    }

  // TODO: Convert local to ROI?

  vtkNew<vtkPoints> roiPoints;
  roiPoints->SetNumberOfPoints(numberOfPoints);

  int pointIndex = 0;

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  this->GetOriginWorld(origin_World);

  // Origin
  roiPoints->SetPoint(ORIGIN_POINT, origin_World); // TODO: this should not be origin

  bool requiredPointsPlaced = false;
  if (numberOfPoints > this->RequiredNumberOfControlPoints)
    {
    requiredPointsPlaced = true;
    }
  else if (numberOfPoints == this->RequiredNumberOfControlPoints)
    {
    requiredPointsPlaced = true;
    for (ControlPoint* controlPoint : this->ControlPoints)
      {
      if (controlPoint->PositionStatus != vtkMRMLMarkupsROINode::PositionDefined)
        {
        requiredPointsPlaced = false;
        break;
        }
      }
    }

  if (numberOfPoints > 1)
    {
    requiredPointsPlaced = true;
    }

  if (requiredPointsPlaced)
  {
    double xAxis[3] = { 0.0, 0.0, 0.0 };
    this->GetXAxisWorld(xAxis); // TODO: should do this in local coordinates
    vtkMath::MultiplyScalar(xAxis, this->SideLengths[0]*0.5);

    double yAxis[3] = { 0.0, 0.0, 0.0 };
    this->GetYAxisWorld(yAxis); // TODO: should do this in local coordinates
    vtkMath::MultiplyScalar(yAxis, this->SideLengths[1]*0.5);

    double zAxis[3] = { 0.0, 0.0, 0.0 };
    this->GetZAxisWorld(zAxis); // TODO: should do this in local coordinates
    vtkMath::MultiplyScalar(zAxis, this->SideLengths[2]*0.5);

    for (int i = 0; i < numberOfPoints; ++i)
      {
      double point_Local[3] = { 0.0, 0.0, 0.0 };
      vtkMath::Add(origin_World, point_Local, point_Local); // TODO: local origin
      switch (i)
        {
        case LR_POINT_1: // -X points
        case LAI_CORNER_POINT:
        case LPI_CORNER_POINT:
        case LAS_CORNER_POINT:
        case LPS_CORNER_POINT:
          vtkMath::Subtract(point_Local, xAxis, point_Local);
          break;
        case LR_POINT_2: // +X points
        case RAI_CORNER_POINT:
        case RPI_CORNER_POINT:
        case RAS_CORNER_POINT:
        case RPS_CORNER_POINT:
          vtkMath::Add(point_Local, xAxis, point_Local);
        default:
          break;
        }

      switch (i)
        {
        case AP_POINT_1: // -Y points
        case LPI_CORNER_POINT:
        case RPI_CORNER_POINT:
        case LPS_CORNER_POINT:
        case RPS_CORNER_POINT:
          vtkMath::Subtract(point_Local, yAxis, point_Local);
          break;
        case AP_POINT_2: // +Y points
        case LAI_CORNER_POINT:
        case RAI_CORNER_POINT:
        case LAS_CORNER_POINT:
        case RAS_CORNER_POINT:
          vtkMath::Add(point_Local, yAxis, point_Local);
        default:
          break;
        }

      switch (i)
        {
        case IS_POINT_1: // -Z points
        case LAI_CORNER_POINT:
        case RAI_CORNER_POINT:
        case LPI_CORNER_POINT:
        case RPI_CORNER_POINT:
          vtkMath::Subtract(point_Local, zAxis, point_Local);
          break;
        case IS_POINT_2: // +Z points
        case LAS_CORNER_POINT:
        case RAS_CORNER_POINT:
        case LPS_CORNER_POINT:
        case RPS_CORNER_POINT:
          vtkMath::Add(point_Local, zAxis, point_Local);
        default:
          break;
        }

      roiPoints->SetPoint(i, point_Local);
      }
  }

  this->SetControlPointPositionsWorld(roiPoints);
  for (int i = 0; i < this->GetNumberOfControlPoints(); ++i)
    {
    this->ControlPoints[i]->PositionStatus = positionStatus;
    }


  //////////////////////////////////////////////////////////////////

  /*double sideLength[3] = { 0.0,0.0,0.0 }*/;
  /*this->GetSideLength(0);*/

  //double bounds_Local[3] = { 1.0, 1.0, 1.0 };
  //for (int i = 1; i < this->GetNumberOfControlPoints(); ++i)
  //  {
  //  double* position_Local = this->GetNthControlPointPosition(i);
  //  for (int i = 0; i < 3; ++i)
  //    {
  //    bounds_Local[i] = std::max(bounds_Local[i], std::abs(origin_Local[i] - position_Local[i]));
  //    }
  //  }

  //for (int i = 1; i < this->GetNumberOfControlPoints(); ++i)
  //  {
  //  double* position_Local = this->GetNthControlPointPosition(i);
  //  switch (i)
  //    {
  //    case 1:
  //    case 2:
  //      bounds_Local[0] = std::abs(origin_Local[0] - position_Local[0]);
  //      break;
  //    case 3:
  //    case 4:
  //      bounds_Local[1] = std::abs(origin_Local[1] - position_Local[1]);
  //      break;
  //    case 5:
  //    case 6:
  //      bounds_Local[2] = std::abs(origin_Local[2] - position_Local[2]);
  //      break;
  //    default:
  //      break;
  //    }
  //  }

  //for (int i = 1; i < NUMBER_OF_BOX_CONTROL_POINTS; ++i)
  //  {
  //  double position_Local[3] = { origin_Local[0], origin_Local[1], origin_Local[2] };
  //  switch (i)
  //    {
  //    case 1:
  //      position_Local[0] -= bounds_Local[0];
  //      break;
  //    case 2:
  //      position_Local[0] += bounds_Local[0];
  //      break;
  //    case 3:
  //      position_Local[1] -= bounds_Local[1];
  //      break;
  //    case 4:
  //      position_Local[1] += bounds_Local[1];
  //      break;
  //    case 5:
  //      position_Local[2] -= bounds_Local[2];
  //      break;
  //    case 6:
  //      position_Local[2] += bounds_Local[2];
  //      break;
  //    default:
  //      break;
  //    }
  //  int status = PositionDefined;
  //  if (i < this->GetNumberOfControlPoints())
  //    {
  //    status = this->GetNthControlPointPositionStatus(i);
  //    }
  //  Superclass::SetNthControlPointPosition(i, position_Local[0], position_Local[1], position_Local[2], status);
  //  }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateROIFromControlPoints(int index/*=-1*/,
  const double x/*=0.0*/, const double y/*=0.0*/, const double z/*=0.0*/,
  int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  if (this->IsUpdatingROIFromControlPoints)
    {
    return;
    }
  this->IsUpdatingROIFromControlPoints = true;

  MRMLNodeModifyBlocker blocker(this);

  vtkTimeStamp timestamp;
  timestamp.Modified();
  this->ROIUpdatedTime = timestamp.GetMTime();

  switch (this->ROIType)
    {
    case BOX:
      this->UpdateBoxROIFromControlPoints(index, x, y, z, positionStatus);
      break;
    case SPHERE:
      this->UpdateSphereROIFromControlPoints();
      break;
    default:
      break;
    }

  this->UpdateControlPointsFromROI(positionStatus);
  this->IsUpdatingROIFromControlPoints = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::ResetROI()
{
  this->ROIToLocalMatrix->Identity();

  //TODO
  this->Origin[0] = 0.0;
  this->Origin[0] = 0.0;
  this->Origin[0] = 0.0;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoxROIFromControlPoints(int index/*=-1*/,
  const double x/*=0.0*/, const double y/*=0.0*/, const double z/*=0.0*/,
  int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  // TODO: WORLD?
  vtkPoints* points = this->CurveInputPoly->GetPoints();

  vtkNew<vtkPoints> newPoints;
  newPoints->DeepCopy(points);

  this->ResetROI();

  // This is "BOX"/"CORNER" ROI code.
  if (points->GetNumberOfPoints() == 0)
    {
    return;
    }

  points->GetPoint(0, this->Origin);

  if (points->GetNumberOfPoints() < this->RequiredNumberOfControlPoints) // TODO
    {
    double bounds[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
    for (int index = 0; index < points->GetNumberOfPoints(); ++index)
      {
      double* point = points->GetPoint(index);
      for (int i = 0; i < 3; ++i)
        {
        bounds[2 * i] = std::min(bounds[2 * i], point[i]);
        bounds[2 * i + 1] = std::max(bounds[2 * i + 1], point[i]);
        }
      }
    this->SideLengths[0] = 2*std::abs(bounds[1] - bounds[0]);
    this->SideLengths[1] = 2*std::abs(bounds[3] - bounds[2]);
    this->SideLengths[2] = 2*std::abs(bounds[5] - bounds[4]);
    }
  else
    {
    double point[3] = { x,y,z };
    double pointToOrigin[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(this->Origin, point, pointToOrigin);
    switch (index)
      {
      case ORIGIN_POINT:
        this->Origin[0] = x;
        this->Origin[1] = y;
        this->Origin[2] = z;
        break;
      case LR_POINT_1:
      case LR_POINT_2:
        this->SideLengths[0] = std::abs(2 * pointToOrigin[0]);
        break;
      case AP_POINT_1:
      case AP_POINT_2:
        this->SideLengths[1] = std::abs(2 * pointToOrigin[1]);
        break;
      case IS_POINT_1:
      case IS_POINT_2:
        this->SideLengths[2] = std::abs(2 * pointToOrigin[2]);
        break;
      case LAI_CORNER_POINT:
      case RAI_CORNER_POINT:
      case LPI_CORNER_POINT:
      case RPI_CORNER_POINT:
      case LAS_CORNER_POINT:
      case RAS_CORNER_POINT:
      case LPS_CORNER_POINT:
      case RPS_CORNER_POINT:
        this->SideLengths[0] = std::abs(2 * pointToOrigin[0]);
        this->SideLengths[1] = std::abs(2 * pointToOrigin[1]);
        this->SideLengths[2] = std::abs(2 * pointToOrigin[2]);
      default:
        break;
      }
    }

  if (index != 0 && points->GetNumberOfPoints() >= this->RequiredNumberOfControlPoints)
    {
    double origin_World[3] = { 0.0, 0.0, 0.0 };
    this->GetOriginWorld(origin_World);

    //// TODO: Update ROI. Specific point position overrides general ROI size.
    double xAxisPoint_World[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPositionWorld(LR_POINT_2, xAxisPoint_World);
    double xAxis_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(xAxisPoint_World, origin_World, xAxis_World);
    double xLength = vtkMath::Normalize(xAxis_World);

    double yAxisPoint_World[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPositionWorld(AP_POINT_2, yAxisPoint_World);
    double yAxis_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(yAxisPoint_World, origin_World, yAxis_World);
    double yLength = vtkMath::Normalize(yAxis_World);

    double zAxisPoint_World[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPositionWorld(IS_POINT_2, zAxisPoint_World);
    double zAxis_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(zAxisPoint_World, origin_World, zAxis_World);
    double zLength = vtkMath::Normalize(zAxis_World);

    if (xLength > 0.0 && yLength > 0.0 && zLength > 0.0)
      {
      for (int i = 0; i < 3; ++i)
        {
        this->ROIToLocalMatrix->SetElement(0, i, xAxis_World[i]);
        this->ROIToLocalMatrix->SetElement(1, i, yAxis_World[i]);
        this->ROIToLocalMatrix->SetElement(2, i, zAxis_World[i]);
        }
      }
    }

  //if (this->AxisAligned)
  //  {
  //  points->GetPoint(0, this->Origin);

  //  // Bounds style calculation
  //  //double bounds[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
  //  //for (int index = 0; index < points->GetNumberOfPoints(); ++index)
  //  //  {
  //  //  double* point = points->GetPoint(index);
  //  //  for (int i = 0; i < 3; ++i)
  //  //    {
  //  //    bounds[2 * i] = std::min(bounds[2 * i], point[i]);
  //  //    bounds[2 * i + 1] = std::max(bounds[2 * i + 1], point[i]);
  //  //    }
  //  //  }

  //  //this->Origin[0] = (bounds[0] + bounds[1]) / 2.0;
  //  //this->Origin[1] = (bounds[2] + bounds[3]) / 2.0;
  //  //this->Origin[2] = (bounds[4] + bounds[5]) / 2.0;

  //  //this->SideLengths[0] = bounds[1] - bounds[0];
  //  //this->SideLengths[1] = bounds[3] - bounds[2];
  //  //this->SideLengths[2] = bounds[5] - bounds[4];
  //  }
  //else
  //  {
  //  double xAxis_World[3] = { 0.0, 0.0, 0.0 };
  //  double yAxis_World[3] = { 0.0, 0.0, 0.0 };
  //  double zAxis_World[3] = { 0.0, 0.0, 0.0 };
  //  vtkOBBTree::ComputeOBB(points, this->Origin, xAxis_World, yAxis_World, zAxis_World, this->SideLengths);
  //  vtkMath::Normalize(xAxis_World);
  //  vtkMath::Normalize(yAxis_World);
  //  vtkMath::Normalize(zAxis_World);

  //  double bounds[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
  //  this->SideLengths[0] = 0.0;
  //  this->SideLengths[1] = 0.0;
  //  this->SideLengths[2] = 0.0;
  //  for (int index = 0; index < points->GetNumberOfPoints(); ++index)
  //    {
  //    double* point = points->GetPoint(index);
  //    double roiPoint[3] = { 0.0 };
  //    vtkMath::Subtract(point, this->Origin, roiPoint);

  //    this->SideLengths[0] = std::max(std::abs(vtkMath::Dot(roiPoint, xAxis_World))*1.0, this->SideLengths[0]);
  //    this->SideLengths[1] = std::max(std::abs(vtkMath::Dot(roiPoint, yAxis_World))*1.0, this->SideLengths[1]);
  //    this->SideLengths[2] = std::max(std::abs(vtkMath::Dot(roiPoint, zAxis_World))*1.0, this->SideLengths[2]);
  //    }

  //  double multiplier = 0.5;

  //  double originXOffset[3] = { 0.0 };
  //  vtkMath::Add(xAxis_World, originXOffset, originXOffset);
  //  vtkMath::MultiplyScalar(originXOffset, this->SideLengths[0] * multiplier);
  //  vtkMath::Add(this->Origin, originXOffset, this->Origin);

  //  double originYOffset[3] = { 0.0 };
  //  vtkMath::Add(yAxis_World, originYOffset, originYOffset);
  //  vtkMath::MultiplyScalar(originYOffset, this->SideLengths[1] * multiplier);
  //  vtkMath::Add(this->Origin, originYOffset, this->Origin);

  //  double originZOffset[3] = { 0.0 };
  //  vtkMath::Add(zAxis_World, originZOffset, originZOffset);
  //  vtkMath::MultiplyScalar(originZOffset, this->SideLengths[2] * multiplier);
  //  vtkMath::Add(this->Origin, originZOffset, this->Origin);

  //  vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
  //  for (int i = 0; i < 3; ++i)
  //    {
  //    newROIToWorldMatrix->SetElement(0, i, xAxis_World[i]);
  //    newROIToWorldMatrix->SetElement(1, i, yAxis_World[i]);
  //    newROIToWorldMatrix->SetElement(2, i, zAxis_World[i]);
  //    }
  //  }

  /*if (this->ROIType == Sphere)*/
    //{
    //vtkOBBTree::ComputeOBB(points, this->Origin, this->XAxis, this->YAxis, this->ZAxis, this->SideLengths);
    //vtkMath::Normalize(this->XAxis);
    //vtkMath::Normalize(this->YAxis);
    //vtkMath::Normalize(this->ZAxis);

    //double bounds[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
    //for (int index = 0; index < points->GetNumberOfPoints(); ++index)
    //  {
    //  double point[3] = { 0.0, 0.0, 0.0 };
    //  points->GetPoint(index, point);
    //  vtkMath::Subtract(point, this->Origin, point);

    //  double xLength = vtkMath::Dot(point, this->XAxis);
    //  double yLength = vtkMath::Dot(point, this->YAxis);
    //  double zLength = vtkMath::Dot(point, this->ZAxis);
    //  bounds[0] = std::min(bounds[0], xLength);
    //  bounds[1] = std::max(bounds[1], xLength);
    //  bounds[2] = std::min(bounds[2], yLength);
    //  bounds[3] = std::max(bounds[3], yLength);
    //  bounds[4] = std::min(bounds[4], zLength);
    //  bounds[5] = std::max(bounds[5], zLength);
    //  }

    //this->SideLengths[0] = bounds[1] - bounds[0];
    //this->SideLengths[1] = bounds[3] - bounds[2];
    //this->SideLengths[2] = bounds[5] - bounds[4];

    //for (int i = 0; i < 3; ++i)
    //  {
    //  this->Origin[i] += this->XAxis[i] * (this->SideLengths[0] * 0.5);
    //  this->Origin[i] += this->YAxis[i] * (this->SideLengths[1] * 0.5);
    //  this->Origin[i] += this->ZAxis[i] * (this->SideLengths[2] * 0.5);
    //  }
    //}
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateSphereROIFromControlPoints()
{
  vtkPoints* points = this->CurveInputPoly->GetPoints();
  if (!points)
    {
    return;
    }

  this->Origin[0] = 0.0;
  this->Origin[1] = 0.0;
  this->Origin[2] = 0.0;

  int numberOfPoints = points->GetNumberOfPoints();
  for (int index = 0; index < numberOfPoints; ++index)
    {
    double* point = points->GetPoint(index);
    for (int i = 0; i < 3; ++i)
      {
      this->Origin[i] += point[i] / numberOfPoints;
      }
    }

  double maxRadius2 = 0.0;
  for (int index = 0; index < numberOfPoints; ++index)
    {
    double* point = points->GetPoint(index);
    double currentDistance2 = vtkMath::Distance2BetweenPoints(this->Origin, point);
    maxRadius2 = std::max(currentDistance2, maxRadius2);
    }

  double maxDiameter = 2.0 * std::sqrt(maxRadius2);
  this->SideLengths[0] = maxDiameter;
  this->SideLengths[1] = maxDiameter;
  this->SideLengths[2] = maxDiameter;
}
