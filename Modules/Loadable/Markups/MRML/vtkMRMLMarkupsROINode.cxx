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
const int NUMBER_OF_SPHERE_CONTROL_POINTS = 2; // 1 center; 1 radius

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROINode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::vtkMRMLMarkupsROINode()
{
  // Set RequiredNumberOfControlPoints to a very high number to remain
  // in place mode after placing a curve point.
  this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;
  //this->RequiredNumberOfControlPoints = NUMBER_OF_SPHERE_CONTROL_POINTS;
  this->PropertiesLabelText = "";
  this->ROIType = BOX;
  //this->ROIType = SPHERE;

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

  this->IsUpdatingROIFromControlPoints = true;
  Superclass::SetControlPointPositionsWorld(points_World);
  this->IsUpdatingROIFromControlPoints = wasUpdatingFromControlPoints;

  this->UpdateROIFromControlPoints();
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPosition(const int pointIndex,
  const double x, const double y, const double z, int positionStatus/*=PositionDefined*/)
{
  if (this->IsUpdatingROIFromControlPoints)
    {
    Superclass::SetNthControlPointPosition(pointIndex, x, y, z, positionStatus);
    }
  else
    {
    double position_Local[3] = { x, y, z };
    double position_World[3] = { 0.0, 0.0, 0.0 };
    this->TransformPointToWorld(position_Local, position_World);
    this->UpdateROIFromControlPoints(pointIndex, position_World, positionStatus);
    }
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPositionOrientationWorldFromArray(const int pointIndex,
  const double position_World[3], const double orientationMatrix[9], const char* associatedNodeID, int positionStatus/*=PositionDefined*/)
{
  if (this->IsUpdatingROIFromControlPoints)
    {
    Superclass::SetNthControlPointPositionOrientationWorldFromArray(pointIndex,
      position_World, orientationMatrix, associatedNodeID, positionStatus);
    }
  else
    {
    this->UpdateROIFromControlPoints(pointIndex, position_World, positionStatus);
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
    case SPHERE:
      this->RequiredNumberOfControlPoints = NUMBER_OF_SPHERE_CONTROL_POINTS;
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
void vtkMRMLMarkupsROINode::SetSideLengths(const double sideLengths_World[3])
{
  if (!sideLengths_World)
    {
    vtkErrorMacro("SetSideLengths: Invalid origin argument");
    return;
    }
  for (int i = 0; i < 3; ++i)
    {
    this->SideLengths[i] = sideLengths_World[i];
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

  this->IsUpdatingControlPointsFromROI = true;
  {
    MRMLNodeModifyBlocker blocker(this);

    switch (this->ROIType)
    {
    case BOX:
      this->UpdateControlPointsFromBoxROI(positionStatus);
      break;
    case SPHERE:
      break;
    default:
      vtkErrorMacro("vtkMRMLMarkupsROINode: Unknown ROI type");
      break;
    }
  }
  this->IsUpdatingControlPointsFromROI = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromBoxROI(int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  int numberOfPoints = this->GetNumberOfControlPoints();
  if (this->SideLengths[0] > 0.0 && this->SideLengths[1] > 0.0 && this->SideLengths[2] > 0.0)
    {
    numberOfPoints = NUMBER_OF_BOX_CONTROL_POINTS;
    }
  if (numberOfPoints == 0)
    {
    return;
    }

  vtkNew<vtkPoints> roiPoints_World;
  this->GetControlPointPositionsWorld(roiPoints_World);
  roiPoints_World->SetNumberOfPoints(numberOfPoints);

  int pointIndex = 0;

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  this->GetOriginWorld(origin_World);

  // Origin
  roiPoints_World->SetPoint(ORIGIN_POINT, origin_World);

  bool requiredPointsPlaced = false;
  if (numberOfPoints > NUMBER_OF_BOX_CONTROL_POINTS)
    {
    requiredPointsPlaced = true;
    }
  else if (numberOfPoints == NUMBER_OF_BOX_CONTROL_POINTS)
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
        case L_FACE_POINT: // -X points
        case LAI_CORNER_POINT:
        case LPI_CORNER_POINT:
        case LAS_CORNER_POINT:
        case LPS_CORNER_POINT:
          vtkMath::Subtract(point_World, xAxis_World, point_World);
          break;
        case R_FACE_POINT: // +X points
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
        case P_FACE_POINT: // -Y points
        case LPI_CORNER_POINT:
        case RPI_CORNER_POINT:
        case LPS_CORNER_POINT:
        case RPS_CORNER_POINT:
          vtkMath::Subtract(point_World, yAxis_World, point_World);
          break;
        case A_FACE_POINT: // +Y points
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
        case I_FACE_POINT: // -Z points
        case LAI_CORNER_POINT:
        case RAI_CORNER_POINT:
        case LPI_CORNER_POINT:
        case RPI_CORNER_POINT:
          vtkMath::Subtract(point_World, zAxis_World, point_World);
          break;
        case S_FACE_POINT: // +Z points
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
    if (i < this->GetNumberOfControlPoints() - 1)
    {
      this->ControlPoints[i]->PositionStatus = PositionDefined;
    }
    else
    {
      this->ControlPoints[i]->PositionStatus = positionStatus;
    }
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateROIFromControlPoints(int index/*=-1*/,
  const double position_World[3]/*=nullptr*/, int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  if (this->IsUpdatingROIFromControlPoints)
    {
    return;
    }

  this->IsUpdatingROIFromControlPoints = true;
  {
    MRMLNodeModifyBlocker blocker(this);

    vtkTimeStamp timestamp;
    timestamp.Modified();
    this->ROIUpdatedTime = timestamp.GetMTime();

    switch (this->ROIType)
    {
    case BOX:
      this->UpdateBoxROIFromControlPoints(index, position_World, positionStatus);
      break;
    case SPHERE:
      this->UpdateSphereROIFromControlPoints(index, position_World, positionStatus);
      break;
    default:
      break;
    }

    this->UpdateControlPointsFromROI(positionStatus);
  }
  this->IsUpdatingROIFromControlPoints = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoxROIFromControlPoints(int index/*=-1*/,
  const double position_World[3]/*=nullptr*/, int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  vtkPoints* points = this->CurveInputPoly->GetPoints();
  if (points->GetNumberOfPoints() == 0)
    {
    return;
    }

  vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
  newROIToWorldMatrix->DeepCopy(this->ROIToWorldMatrix);

  if (points->GetNumberOfPoints() < this->RequiredNumberOfControlPoints) // TODO
    {
    double origin_World[3] = { 0.0, 0.0, 0.0 };
    if (index == 0)
      {
      vtkMath::Add(origin_World, position_World, origin_World);
      }
    else
      {
      points->GetPoint(ORIGIN_POINT, origin_World);
      }

    this->SideLengths[0] = 0.0;
    this->SideLengths[1] = 0.0;
    this->SideLengths[2] = 0.0;
    for (int index = 1; index < points->GetNumberOfPoints(); ++index)
      {
      double* point = points->GetPoint(index);
      double currentPointToOrigin_World[3] = { 0.0, 0.0, 0.0 };
      vtkMath::Subtract(point, origin_World, currentPointToOrigin_World);

      double xAxis_World[3] = { 0.0, 0.0, 0.0 };
      this->GetXAxisWorld(xAxis_World);
      double xAxisLength = 2.0 * std::abs(vtkMath::Dot(xAxis_World, currentPointToOrigin_World));

      double yAxis_World[3] = { 0.0, 0.0, 0.0 };
      this->GetYAxisWorld(yAxis_World);
      double yAxisLength = 2.0 * std::abs(vtkMath::Dot(yAxis_World, currentPointToOrigin_World));

      double zAxis_World[3] = { 0.0, 0.0, 0.0 };
      this->GetZAxisWorld(zAxis_World);
      double zAxisLength = 2.0 * std::abs(vtkMath::Dot(zAxis_World, currentPointToOrigin_World));

      this->SideLengths[0] = std::max(xAxisLength, this->SideLengths[0]);
      this->SideLengths[1] = std::max(yAxisLength, this->SideLengths[2]);
      this->SideLengths[2] = std::max(zAxisLength, this->SideLengths[2]);

      if (points->GetNumberOfPoints() >= 3)
        {
        this->SideLengths[0] = std::max(xAxisLength, 10.0);
        this->SideLengths[1] = std::max(yAxisLength, 10.0);
        this->SideLengths[2] = std::max(zAxisLength, 10.0);
        positionStatus = vtkMRMLMarkupsNode::PositionDefined;
        }

      }
    for (int i = 0; i < 3; ++i)
      {
      newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
      }

    if (index >= 0 && position_World)
      {
      this->SetNthControlPointPositionFromArray(index, position_World, positionStatus);
      }
    }
  else if (index >= 0)
    {
    double origin_World[3] = { 0.0, 0.0, 0.0 };
    points->GetPoint(ORIGIN_POINT, origin_World);

    double pointToOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(position_World, origin_World, pointToOrigin_World);

    double xAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetXAxisWorld(xAxis_World);
    double xAxisLength = 2.0 * std::abs(vtkMath::Dot(xAxis_World, pointToOrigin_World));

    double yAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetYAxisWorld(yAxis_World);
    double yAxisLength = 2.0 * std::abs(vtkMath::Dot(yAxis_World, pointToOrigin_World));

    double zAxis_World[3] = { 0.0, 0.0, 0.0 };
    this->GetZAxisWorld(zAxis_World);
    double zAxisLength = 2.0 * std::abs(vtkMath::Dot(zAxis_World, pointToOrigin_World));

    double originalSideLengths[3] = { this->SideLengths[0], this->SideLengths[1], this->SideLengths[2] };

    switch (index)
      {
      case ORIGIN_POINT:
        newROIToWorldMatrix->SetElement(0, 3, position_World[0]);
        newROIToWorldMatrix->SetElement(1, 3, position_World[1]);
        newROIToWorldMatrix->SetElement(2, 3, position_World[2]);
        break;
      case L_FACE_POINT:
      case R_FACE_POINT:
        this->SideLengths[0] = xAxisLength;
        break;
      case P_FACE_POINT:
      case A_FACE_POINT:
        this->SideLengths[1] = yAxisLength;
        break;
      case I_FACE_POINT:
      case S_FACE_POINT:
        this->SideLengths[2] = zAxisLength;
        break;
      case LAI_CORNER_POINT:
      case RAI_CORNER_POINT:
      case LPI_CORNER_POINT:
      case RPI_CORNER_POINT:
      case LAS_CORNER_POINT:
      case RAS_CORNER_POINT:
      case LPS_CORNER_POINT:
      case RPS_CORNER_POINT:
        this->SideLengths[0] = xAxisLength;
        this->SideLengths[1] = yAxisLength;
        this->SideLengths[2] = zAxisLength;
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
    this->GetNthControlPointPositionWorld(R_FACE_POINT, xAxisPoint_World);
    double xAxis_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(xAxisPoint_World, origin_World, xAxis_World);
    double xLength = vtkMath::Normalize(xAxis_World);

    double yAxisPoint_World[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPositionWorld(A_FACE_POINT, yAxisPoint_World);
    double yAxis_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(yAxisPoint_World, origin_World, yAxis_World);
    double yLength = vtkMath::Normalize(yAxis_World);

    double zAxisPoint_World[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPositionWorld(I_FACE_POINT, zAxisPoint_World);
    double zAxis_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(zAxisPoint_World, origin_World, zAxis_World);
    double zLength = vtkMath::Normalize(zAxis_World);

    if (xLength > 0.0 && yLength > 0.0 && zLength > 0.0)
      {
      for (int i = 0; i < 3; ++i)
        {
        //newROIToWorldMatrix->SetElement(i, 0, xAxis_World[i]);
        //newROIToWorldMatrix->SetElement(i, 1, yAxis_World[i]);
        //newROIToWorldMatrix->SetElement(i, 2, zAxis_World[i]);
        newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
        }
      }
    }
  this->ROIToWorldMatrix->DeepCopy(newROIToWorldMatrix);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateSphereROIFromControlPoints(int pointIndex, const double position_World[3]/* = nullptr*/,
  int positionStatus/* = vtkMRMLMarkupsNode::PositionDefined*/)
{
  if (pointIndex >= 0 && position_World)
    {
    Superclass::SetNthControlPointPositionWorldFromArray(pointIndex, position_World, positionStatus);
    }

  vtkPoints* points = this->CurveInputPoly->GetPoints();

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  this->GetNthControlPointPositionWorld(ORIGIN_POINT, origin_World);

  double radius = 0.0;
  for (int i = 1; i < points->GetNumberOfPoints(); ++i)
    {
    double pointToOrigin_World[3] = { 0.0, 0.0, 0.0 };
    double currentPoint_World[3] = { 0.0, 0.0, 0.0 };
    points->GetPoint(i, currentPoint_World);
    vtkMath::Subtract(currentPoint_World, origin_World, pointToOrigin_World);
    radius = std::max(radius, vtkMath::Norm(pointToOrigin_World));
    }
  this->SideLengths[0] = 2*radius;
  this->SideLengths[1] = 2*radius;
  this->SideLengths[2] = 2*radius;

  vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
  for (int i = 0; i < 3; ++i)
    {
    newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
    }
  this->ROIToWorldMatrix->DeepCopy(newROIToWorldMatrix);
}
