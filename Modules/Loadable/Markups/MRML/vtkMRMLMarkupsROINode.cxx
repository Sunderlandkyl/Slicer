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

const int NUMBER_OF_BOX_CONTROL_POINTS = 7;

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROINode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::vtkMRMLMarkupsROINode()
{
  // Set RequiredNumberOfControlPoints to a very high number to remain
  // in place mode after placing a curve point.
  this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;
  this->PropertiesLabelText = "";
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
void vtkMRMLMarkupsROINode::GetOrigin(double origin_Local[3])
{
  if (!origin_Local)
    {
    vtkErrorMacro("GetOrigin: Invalid origin argument");
    return;
    }

  origin_Local[0] = 0.0;
  origin_Local[1] = 0.0;
  origin_Local[2] = 0.0;

  if (this->GetNumberOfControlPoints() < 1)
    {
    vtkWarningMacro("GetOrigin: Not enough points to define plane origin");
    return;
    }

  this->GetNthControlPointPosition(0, origin_Local);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetOrigin(const double origin_Local[3])
{
  if (!origin_Local)
    {
    vtkErrorMacro("SetOrigin: Invalid origin argument");
    return;
    }

  this->SetNthControlPointPosition(0, origin_Local[0], origin_Local[1], origin_Local[2]);
}

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

const int ORIGIN_POINT = 0;
const int L_POINT = 1;
const int R_POINT = 2;
const int P_POINT = 3;
const int A_POINT = 4;
const int I_POINT = 5;
const int S_POINT = 6;

#include <vtkLine.h>
static bool updating = false;
//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoxROIControlPoints()
{
  //double snapVectorLocal[3] = { 0,0,0 };
  //switch (pointIndex)
  //  {
  //  case ORIGIN_POINT:
  //    break;
  //  case L_POINT:
  //  case R_POINT:
  //    snapVectorLocal[0] = 1;
  //    snapVectorLocal[1] = 0;
  //    snapVectorLocal[2] = 0;
  //    break;
  //  case P_POINT:
  //  case A_POINT:
  //    snapVectorLocal[0] = 0;
  //    snapVectorLocal[1] = 1;
  //    snapVectorLocal[2] = 0;
  //    break;
  //  case I_POINT:
  //  case S_POINT:
  //    snapVectorLocal[0] = 0;
  //    snapVectorLocal[1] = 0;
  //    snapVectorLocal[2] = 1;
  //    break;
  //  default:
  //    break;
  //  }

  //if (pointIndex != ORIGIN_POINT)
  //  {
  //  double pointVectorLocal[3] = {0, 0, 0};
  //  double originLocal[3] = {0, 0, 0};
  //  vtkMath::Subtract(posLocal, originLocal, pointVectorLocal);
  //  vtkMath::MultiplyScalar(snapVectorLocal, vtkMath::Dot(pointVectorLocal, snapVectorLocal));
  //  vtkMath::Add(snapVectorLocal, originLocal, posLocal);
  //  }
  updating = true;
  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(origin_Local);

  double bounds_Local[3] = { 1.0, 1.0, 1.0 };
  for (int i = 1; i < this->GetNumberOfControlPoints(); ++i)
    {
    double* position_Local = this->GetNthControlPointPosition(i);
    for (int i = 0; i < 3; ++i)
      {
      bounds_Local[i] = std::max(bounds_Local[i], std::abs(origin_Local[i] - position_Local[i]));
      }
    }

  for (int i = 1; i < this->GetNumberOfControlPoints(); ++i)
    {
    if (this->GetNthControlPointPositionStatus(i) != PositionDefined)
      {
      double* position_Local = this->GetNthControlPointPosition(i);
      switch (i)
        {
        case 1:
        case 2:
          bounds_Local[0] = std::abs(origin_Local[0] - position_Local[0]);
          break;
        case 3:
        case 4:
          bounds_Local[1] = std::abs(origin_Local[1] - position_Local[1]);
          break;
        case 5:
        case 6:
          bounds_Local[2] = std::abs(origin_Local[2] - position_Local[2]);
          break;
        default:
          break;
        }
      }
    }

  for (int i = 1; i < NUMBER_OF_BOX_CONTROL_POINTS; ++i)
    {
    double position_Local[3] = { origin_Local[0], origin_Local[1], origin_Local[2] };
    switch (i)
      {
      case 1:
        position_Local[0] -= bounds_Local[0];
        break;
      case 2:
        position_Local[0] += bounds_Local[0];
        break;
      case 3:
        position_Local[1] -= bounds_Local[1];
        break;
      case 4:
        position_Local[1] += bounds_Local[1];
        break;
      case 5:
        position_Local[2] -= bounds_Local[2];
        break;
      case 6:
        position_Local[2] += bounds_Local[2];
        break;
      default:
        break;
      }
    int status = PositionDefined;
    if (i < this->GetNumberOfControlPoints())
      {
      status = this->GetNthControlPointPositionStatus(i);
      }
    Superclass::SetNthControlPointPosition(i, position_Local[0], position_Local[1], position_Local[2], status);
    }
  updating = false;
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPosition(const int pointIndex,
  const double x, const double y, const double z, int positionStatus/*=PositionDefined*/)
{
  if (updating)
  {
    return;
  }
  Superclass::SetNthControlPointPosition(pointIndex, x, y, z, positionStatus);
  this->UpdateBoxROIControlPoints();
}

//-----------------------------------------------------------
void vtkMRMLMarkupsROINode::SetNthControlPointPositionOrientationWorldFromArray(const int pointIndex,
  const double pos[3], const double orientationMatrix[9], const char* associatedNodeID, int positionStatus/*=PositionDefined*/)
{
  if (updating)
  {
    return;
  }
  Superclass::SetNthControlPointPositionOrientationWorldFromArray(pointIndex,
    pos, orientationMatrix, associatedNodeID, positionStatus);
  this->UpdateBoxROIControlPoints();
}