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

const int NUMBER_OF_BOX_CONTROL_POINTS = 8;

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROINode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::vtkMRMLMarkupsROINode()
{
  // Set RequiredNumberOfControlPoints to a very high number to remain
  // in place mode after placing a curve point.
  this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;
  this->PropertiesLabelText = "";
  //this->ROIType = Box;
  this->ROIType = Sphere;

  this->Origin[0] = 0.0;
  this->Origin[1] = 0.0;
  this->Origin[1] = 0.0;

  this->XAxis[0] = 1.0;
  this->XAxis[1] = 0.0;
  this->XAxis[2] = 0.0;

  this->YAxis[0] = 0.0;
  this->YAxis[1] = 1.0;
  this->YAxis[2] = 0.0;

  this->ZAxis[0] = 0.0;
  this->ZAxis[1] = 0.0;
  this->ZAxis[2] = 1.0;

  this->SideLengths[0] = 0.0;
  this->SideLengths[1] = 0.0;
  this->SideLengths[2] = 0.0;

  this->Bounds[0] = 0.0;
  this->Bounds[1] = -1.0;
  this->Bounds[2] = 0.0;
  this->Bounds[3] = -1.0;
  this->Bounds[4] = 0.0;
  this->Bounds[5] = -1.0;

  this->AxisAligned = true;

  this->IsUpdatingControlPoints = false;
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
void vtkMRMLMarkupsROINode::SetNthControlPointPosition(const int pointIndex,
  const double x, const double y, const double z, int positionStatus/*=PositionDefined*/)
{
  Superclass::SetNthControlPointPosition(pointIndex, x, y, z, positionStatus);
  //this->UpdateControlPointsFromROI();
  //this->UpdateROIFromControlPoints();
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPositionOrientationWorldFromArray(const int pointIndex,
  const double pos[3], const double orientationMatrix[9], const char* associatedNodeID, int positionStatus/*=PositionDefined*/)
{
  Superclass::SetNthControlPointPositionOrientationWorldFromArray(pointIndex,
    pos, orientationMatrix, associatedNodeID, positionStatus);
  //this->UpdateControlPointsFromROI();
  //this->UpdateROIFromControlPoints();
}

////----------------------------------------------------------------------------
//void vtkMRMLMarkupsROINode::GetOrigin(double origin_Local[3])
//{
//  if (!origin_Local)
//    {
//    vtkErrorMacro("GetOrigin: Invalid origin argument");
//    return;
//    }
//
//  origin_Local[0] = 0.0;
//  origin_Local[1] = 0.0;
//  origin_Local[2] = 0.0;
//
//  if (this->GetNumberOfControlPoints() < 1)
//    {
//    vtkWarningMacro("GetOrigin: Not enough points to define plane origin");
//    return;
//    }
//
//  this->GetNthControlPointPosition(0, origin_Local);
//}

////----------------------------------------------------------------------------
//void vtkMRMLMarkupsROINode::SetOrigin(const double origin_Local[3])
//{
//  if (!origin_Local)
//    {
//    vtkErrorMacro("SetOrigin: Invalid origin argument");
//    return;
//    }
//
//  this->SetNthControlPointPosition(0, origin_Local[0], origin_Local[1], origin_Local[2]);
//}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetOriginWorld(double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("GetOriginWorld: Invalid origin argument");
    return;
    }

  origin_World[0] = 0.0;
  origin_World[1] = 0.0;
  origin_World[2] = 0.0;

  if (this->GetNumberOfControlPoints() < 1)
    {
    vtkWarningMacro("GetOriginWorld: Not enough points to define plane origin");
    return;
    }

  this->GetNthControlPointPositionWorld(0, origin_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetOriginWorld(const double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("SetOriginWorld: Invalid origin argument");
    return;
    }

  this->SetNthControlPointPosition(0, origin_World[0], origin_World[1], origin_World[2]);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromROI()
{
  // Don't do that currently
  return;
  switch (this->ROIType)
    {
    case Box:
      this->UpdateControlPointsFromBoxROI();
      break;
    default:
      vtkErrorMacro("vtkMRMLMarkupsROINode: Unknown ROI type")
      break;
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromBoxROI()
{
  // Don't do that currently
  return;

  ////vtkMRMLMarkupsDisplayNode::SafeDownCast(this->GetDisplayNode())->GetActiveComponent

  //double origin[3] = { 0.0, 0.0, 0.0 };
  //this->GetOrigin(origin);

  //double xAxis[3] = { 0, 0, 0 };
  //this->GetDirection(0, xAxis);
  //double yAxis[3] = { 0, 0, 0 };
  //this->GetDirection(1, yAxis);
  //double zAxis[3] = { 0, 0, 0 };
  //this->GetDirection(2, zAxis);

  //double sideLengths[3] = { 0.0, 0.0, 0.0 };
  //this->GetSideLengths(sideLengths);

  //vtkMath::MultiplyScalar(xAxis, 0.5 * sideLengths[0]);
  //vtkMath::MultiplyScalar(yAxis, 0.5 * sideLengths[1]);
  //vtkMath::MultiplyScalar(zAxis, 0.5 * sideLengths[2]);

  //vtkNew<vtkPoints> pointsWorld;
  //this->GetControlPointPositionsWorld(pointsWorld);
  //pointsWorld->SetNumberOfPoints(9);
  //for (int i = 0; i < 9; ++i)
  //  {
  //  double point[3] = { origin[0], origin[1], origin[2] };
  //
  //  switch (i)
  //    {
  //    case 1:
  //    case 3:
  //    case 5:
  //    case 7:
  //      vtkMath::Subtract(point, xAxis, point);
  //    default:
  //      vtkMath::Add(point, xAxis, point);
  //    }

  //  switch (i)
  //    {
  //    case 1:
  //    case 2:
  //    case 5:
  //    case 6:
  //      vtkMath::Subtract(point, yAxis, point);
  //    default:
  //      vtkMath::Add(point, yAxis, point);
  //    }

  //  switch (i)
  //    {
  //    case 1:
  //    case 2:
  //    case 3:
  //    case 4:
  //      vtkMath::Subtract(point, zAxis, point);
  //    default:
  //      vtkMath::Add(point, zAxis, point);
  //    }

  //  pointsWorld->SetPoint(i, origin);
  //  }
  //this->SetControlPointPositionsWorld(pointsWorld);
}

const int ORIGIN_POINT = 0;
const int L_POINT = 1;
const int R_POINT = 2;
const int P_POINT = 3;
const int A_POINT = 4;
const int I_POINT = 5;
const int S_POINT = 6;


//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoxROIControlPoints()
{
  this->IsUpdatingControlPoints = true;
  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(origin_Local);

  //double xAxis_Local[3] = { 1.0, 0.0, 0.0 };
  //this->GetDirection(0, xAxis_Local);

  //double yAxis_Local[3] = { 0.0, 1.0, 0.0 };
  //this->GetDirection(1, yAxis_Local);

  //double zAxis_Local[3] = { 0.0, 0.0, 1.0 };
  //this->GetDirection(2, zAxis_Local);

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
  this->IsUpdatingControlPoints = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateROIFromControlPoints()
{
  vtkPoints* points = this->CurveInputPoly->GetPoints();
  if (!points || this->ROIUpdatedTime >= points->GetMTime())
  {
    return;
  }

  vtkTimeStamp timestamp;
  timestamp.Modified();
  this->ROIUpdatedTime = timestamp.GetMTime();

  if (this->AxisAligned)
    {
    this->XAxis[0] = 1.0;
    this->XAxis[1] = 0.0;
    this->XAxis[2] = 0.0;

    this->YAxis[0] = 0.0;
    this->YAxis[1] = 1.0;
    this->YAxis[2] = 0.0;

    this->ZAxis[0] = 0.0;
    this->ZAxis[1] = 0.0;
    this->ZAxis[2] = 1.0;

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

    this->Origin[0] = (bounds[0] + bounds[1]) / 2.0;
    this->Origin[1] = (bounds[2] + bounds[3]) / 2.0;
    this->Origin[2] = (bounds[4] + bounds[5]) / 2.0;

    this->SideLengths[0] = bounds[1] - bounds[0];
    this->SideLengths[1] = bounds[3] - bounds[2];
    this->SideLengths[2] = bounds[5] - bounds[4];
    }
  else
    {
    vtkOBBTree::ComputeOBB(points, this->Origin, this->XAxis, this->YAxis, this->ZAxis, this->SideLengths);
    vtkMath::Normalize(this->XAxis);
    vtkMath::Normalize(this->YAxis);
    vtkMath::Normalize(this->ZAxis);

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

    this->Origin[0] = (bounds[0] + bounds[1]) / 2.0;
    this->Origin[1] = (bounds[2] + bounds[3]) / 2.0;
    this->Origin[2] = (bounds[4] + bounds[5]) / 2.0;

    this->SideLengths[0] = bounds[1] - bounds[0];
    this->SideLengths[1] = bounds[3] - bounds[2];
    this->SideLengths[2] = bounds[5] - bounds[4];
    }

  if (this->ROIType == Sphere)
    {
    double maxSideLength = std::max(this->SideLengths[0], this->SideLengths[1]);
    maxSideLength = std::max(maxSideLength, this->SideLengths[2]);
    for (int i = 0; i < 3; ++i)
      {
      this->SideLengths[i] = maxSideLength;
      }
    }


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