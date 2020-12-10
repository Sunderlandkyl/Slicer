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

const int NUMBER_OF_BOX_CONTROL_POINTS = 15; // 1 center; 8 corners; 6 faces

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

  this->SideLengths[0] = 0.0;
  this->SideLengths[1] = 0.0;
  this->SideLengths[2] = 0.0;

  this->IsUpdatingControlPointsFromROI = false;
  this->IsUpdatingROIFromControlPoints = false;
  this->ROIUpdatedTime = 0;

  this->CurveInputPoly->GetPoints()->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);

  this->InteractionHandleToWorldMatrix = this->ROIToWorldMatrix;
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
  if (axisIndex < 0 || axisIndex >= 3)
    {
    vtkErrorMacro("Invalid axisIndex. Must be 0, 1, or 2.");
    return;
    }

  double axis4_World[4] = { 0.0, 0.0, 0.0, 0.0 };
  axis4_World[axisIndex] = 1.0;
  this->ROIToWorldMatrix->MultiplyPoint(axis4_World, axis4_World);

  for (int i = 0; i < 3; ++i)
    {
    axis_World[i] = axis4_World[i];
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetXAxisLocal(double axis_Local[3])
{
  this->GetAxisLocal(0, axis_Local);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetYAxisLocal(double axis_Local[3])
{
  this->GetAxisLocal(1, axis_Local);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetZAxisLocal(double axis_Local[3])
{
  this->GetAxisLocal(2, axis_Local);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetAxisLocal(int axisIndex, double axis_Local[3])
{
  if (axisIndex < 0 || axisIndex >= 3)
    {
    vtkErrorMacro("Invalid axisIndex. Must be 0, 1, or 2.");
    return;
    }

  // TODO
  //double axis4_Local[4] = { 0.0, 0.0, 0.0, 0.0 };
  //axis4_Local[axisIndex] = 1.0;
  //

  //for (int i = 0; i < 3; ++i)
  //  {
  //  axis_Local[i] = axis4_Local[i];
  //  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData)
{
  Superclass::ProcessMRMLEvents(caller, event, callData);
  if (caller == this->CurveInputPoly->GetPoints() || caller == this->GetParentTransformNode())
    {
    this->UpdateROIFromControlPoints();
    }
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
  this->UpdateControlPointsFromROI();
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

  double origin_World4[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->ROIToWorldMatrix->MultiplyPoint(origin_World4, origin_World4);

  origin_World[0] = origin_World4[0];
  origin_World[1] = origin_World4[1];
  origin_World[2] = origin_World4[2];
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetOriginWorld(const double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("SetOriginWorld: Invalid origin argument");
    return;
    }

  for (int i = 0; i < 3; ++i)
    {
    this->ROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
    }

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
  else if (this->SideLengths[0] <= 0.0 || this->SideLengths[1] <= 0.0 || this->SideLengths[2] <= 0.0)
    {
    numberOfPoints = this->GetNumberOfControlPoints();
    }

  vtkNew<vtkPoints> roiPoints_World;
  roiPoints_World->SetNumberOfPoints(numberOfPoints);

  int pointIndex = 0;

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  this->GetOriginWorld(origin_World);

  // Origin
  roiPoints_World->SetPoint(ORIGIN_POINT, origin_World);

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

  //if (numberOfPoints < this->RequiredNumberOfControlPoints)
  //  {
  //  return;
  //  }

  if (requiredPointsPlaced)
  {
    double xAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetXAxisWorld(xAxis_World);
    vtkMath::MultiplyScalar(xAxis_World, this->SideLengths[0]*0.5);

    double yAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetYAxisWorld(yAxis_World);
    vtkMath::MultiplyScalar(yAxis_World, this->SideLengths[1]*0.5);

    double zAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetZAxisWorld(zAxis_World);
    vtkMath::MultiplyScalar(zAxis_World, this->SideLengths[2]*0.5);

    for (int i = 0; i < numberOfPoints; ++i)
      {
      double point_World[3] = { origin_World[0], origin_World[1], origin_World[2] };
      switch (i)
        {
        case LR_POINT_1: // -X points
        case LAI_CORNER_POINT:
        case LPI_CORNER_POINT:
        case LAS_CORNER_POINT:
        case LPS_CORNER_POINT:
          vtkMath::Subtract(point_World, xAxis_World, point_World);
          break;
        case LR_POINT_2: // +X points
        case RAI_CORNER_POINT:
        case RPI_CORNER_POINT:
        case RAS_CORNER_POINT:
        case RPS_CORNER_POINT:
          vtkMath::Add(point_World, xAxis_World, point_World);
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
          vtkMath::Subtract(point_World, yAxis_World, point_World);
          break;
        case AP_POINT_2: // +Y points
        case LAI_CORNER_POINT:
        case RAI_CORNER_POINT:
        case LAS_CORNER_POINT:
        case RAS_CORNER_POINT:
          vtkMath::Add(point_World, yAxis_World, point_World);
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
          vtkMath::Subtract(point_World, zAxis_World, point_World);
          break;
        case IS_POINT_2: // +Z points
        case LAS_CORNER_POINT:
        case RAS_CORNER_POINT:
        case LPS_CORNER_POINT:
        case RPS_CORNER_POINT:
          vtkMath::Add(point_World, zAxis_World, point_World);
        default:
          break;
        }

      roiPoints_World->SetPoint(i, point_World);
      }
  }

  this->SetControlPointPositionsWorld(roiPoints_World);
  for (int i = 0; i < this->GetNumberOfControlPoints(); ++i)
    {
    this->ControlPoints[i]->PositionStatus = positionStatus;
    }
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
  this->ROIToWorldMatrix->Identity();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoxROIFromControlPoints(int index/*=-1*/,
  const double x/*=0.0*/, const double y/*=0.0*/, const double z/*=0.0*/,
  int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  vtkPoints* points = this->CurveInputPoly->GetPoints();
  if (points->GetNumberOfPoints() == 0)
    {
    return;
    }

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
  else if (index >= 0)
    {
    double point_Local[3] = { x,y,z };
    double point_World[3] = { x,y,z };
    this->TransformPointToWorld(point_Local, point_World);
    double pointToOrigin[3] = { 0.0, 0.0, 0.0 };
    points->GetPoint(ORIGIN_POINT, pointToOrigin);
    vtkMath::Subtract(pointToOrigin, point_Local, pointToOrigin);
    switch (index)
      {
      case ORIGIN_POINT:
        this->ROIToWorldMatrix->SetElement(0, 3, point_World[0]);
        this->ROIToWorldMatrix->SetElement(1, 3, point_World[1]);
        this->ROIToWorldMatrix->SetElement(2, 3, point_World[2]);
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
  else
    {
    // Update world matrix
    double origin_World[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPositionWorld(ORIGIN_POINT, origin_World);

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

    vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
    if (xLength > 0.0 && yLength > 0.0 && zLength > 0.0)
      {
      for (int i = 0; i < 3; ++i)
        {
        newROIToWorldMatrix->SetElement(i, 0, xAxis_World[i]);
        newROIToWorldMatrix->SetElement(i, 1, yAxis_World[i]);
        newROIToWorldMatrix->SetElement(i, 2, zAxis_World[i]);
        newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
        }
      }
    this->ROIToWorldMatrix->DeepCopy(newROIToWorldMatrix);
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateROI()
{

}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateSphereROIFromControlPoints()
{
  vtkPoints* points = this->CurveInputPoly->GetPoints();
  if (!points)
    {
    return;
    }

  //this->Origin[0] = 0.0;
  //this->Origin[1] = 0.0;
  //this->Origin[2] = 0.0;

  //int numberOfPoints = points->GetNumberOfPoints();
  //for (int index = 0; index < numberOfPoints; ++index)
  //  {
  //  double* point = points->GetPoint(index);
  //  for (int i = 0; i < 3; ++i)
  //    {
  //    this->Origin[i] += point[i] / numberOfPoints;
  //    }
  //  }

  //double maxRadius2 = 0.0;
  //for (int index = 0; index < numberOfPoints; ++index)
  //  {
  //  double* point = points->GetPoint(index);
  //  double currentDistance2 = vtkMath::Distance2BetweenPoints(this->Origin, point);
  //  maxRadius2 = std::max(currentDistance2, maxRadius2);
  //  }

  //double maxDiameter = 2.0 * std::sqrt(maxRadius2);
  //this->SideLengths[0] = maxDiameter;
  //this->SideLengths[1] = maxDiameter;
  //this->SideLengths[2] = maxDiameter;
}
