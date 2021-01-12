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

// vtkAddon includes
#include <vtkAddonMathUtilities.h>

const int NUMBER_OF_BOX_CONTROL_POINTS = 2; // 1 center; 1 corner; Used for initial ROI definition, then removed
const int NUMBER_OF_SPHERE_CONTROL_POINTS = 2; // 1 center; 1 radius
const int NUMBER_OF_BOUNDING_BOX_CONTROL_POINTS = 1e6; // Any number of points

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROINode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::vtkMRMLMarkupsROINode()
{
  this->PropertiesLabelText = "";

  this->ROIType = BOX;
  this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;

  //this->ROIType = BOUNDING_BOX;
  //this->RequiredNumberOfControlPoints = NUMBER_OF_BOUNDING_BOX_CONTROL_POINTS;

  //this->ROIType = SPHERE;
  //this->RequiredNumberOfControlPoints = NUMBER_OF_SPHERE_CONTROL_POINTS;

  this->SideLengths[0] = 0.0;
  this->SideLengths[1] = 0.0;
  this->SideLengths[2] = 0.0;

  this->IsUpdatingControlPointsFromROI = false;
  this->IsUpdatingROIFromControlPoints = false;
  this->ROIUpdatedTime = 0;

  this->CurveInputPoly->GetPoints()->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
  this->ROIToWorldMatrix = this->InteractionHandleToWorldMatrix;
  this->ROIToWorldMatrix->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
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
  else if (caller == this->ROIToWorldMatrix.GetPointer() && event == vtkCommand::ModifiedEvent)
    {
    this->Modified();
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
    case BOUNDING_BOX:
      this->RequiredNumberOfControlPoints = NUMBER_OF_BOUNDING_BOX_CONTROL_POINTS;
      break;
    case SPHERE:
      this->RequiredNumberOfControlPoints = NUMBER_OF_SPHERE_CONTROL_POINTS;
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

  vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
  newROIToWorldMatrix->DeepCopy(this->ROIToWorldMatrix);

  for (int i = 0; i < 3; ++i)
    {
    newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
    }
  this->ROIToWorldMatrix->DeepCopy(newROIToWorldMatrix);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetSideLengths(const double sideLengths_World[3])
{
  if (!sideLengths_World)
    {
    vtkErrorMacro("SetSideLengths: Invalid side lengths argument");
    return;
    }
  for (int i = 0; i < 3; ++i)
    {
    this->SideLengths[i] = sideLengths_World[i];
    }
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetBoundsROI(double bounds_ROI[6])
{
  vtkNew<vtkMatrix4x4> worldToROIMatrix;
  worldToROIMatrix->DeepCopy(this->ROIToWorldMatrix);
  worldToROIMatrix->Invert();

  double sideLengths[3] = { 0.0, 0.0, 0.0 };
  this->GetSideLengths(sideLengths);
  for (int i = 0; i < 3; ++i)
    {
    bounds_ROI[2 * i] = -sideLengths[i] * 0.5;
    bounds_ROI[2*i+1] =  sideLengths[i] * 0.5;
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateROIFromControlPoints()
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
      this->UpdateBoxROIFromControlPoints();
      break;
    case BOUNDING_BOX:
      this->UpdateBoundingBoxROIFromControlPoints();
      break;
    case SPHERE:
      this->UpdateSphereROIFromControlPoints();
      break;
    default:
      break;
    }
  }
  this->IsUpdatingROIFromControlPoints = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoxROIFromControlPoints()
{
  int numberOfControlPoints = this->GetNumberOfControlPoints();
  if (numberOfControlPoints == 0)
    {
    return;
    }

  vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
  newROIToWorldMatrix->DeepCopy(this->ROIToWorldMatrix);

  double bounds_ROI[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, };
  this->GetBoundsROI(bounds_ROI);

  double origin_ROI[4] = { 0.0, 0.0, 0.0, 1.0 };
  for (int i = 0; i < 3; ++i)
    {
    this->SideLengths[i] = bounds_ROI[2 * i + 1] - bounds_ROI[2 * i];
    origin_ROI[i] = (bounds_ROI[2 * i + 1] + bounds_ROI[2 * i]) / 2.0;
    }

  double origin_World[4] = { 0.0, 0.0, 0.0, 0.0 };
  this->ROIToWorldMatrix->MultiplyPoint(origin_ROI, origin_World);

  for (int i = 0; i < 3; ++i)
    {
    newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
    }

  if (this->GetNumberOfControlPoints() == NUMBER_OF_BOX_CONTROL_POINTS)
    {
    if (this->SideLengths[0] == 0.0)
      {
      this->SideLengths[0] = 100.0;
      }
    if (this->SideLengths[1] == 0.0)
      {
      this->SideLengths[1] = this->SideLengths[0];
      }
    if (this->SideLengths[2] == 0.0)
      {
      this->SideLengths[2] = this->SideLengths[0];
      }
    }


  if (this->GetNumberOfDefinedControlPoints() == NUMBER_OF_BOX_CONTROL_POINTS)
    {
    this->RequiredNumberOfControlPoints = 0;
    this->RemoveAllControlPoints();
    }

  this->ROIToWorldMatrix->DeepCopy(newROIToWorldMatrix);
}


//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoundingBoxROIFromControlPoints()
{
  //double xAxis_World[3] = { 0.0, 0.0, 0.0 };
  //this->GetXAxisWorld(xAxis_World);

  //double yAxis_World[3] = { 0.0, 0.0, 0.0 };
  //this->GetYAxisWorld(yAxis_World);

  //double zAxis_World[3] = { 0.0, 0.0, 0.0 };
  //this->GetZAxisWorld(zAxis_World);

  //this->SideLengths[0] = 0.0;
  //this->SideLengths[1] = 0.0;
  //this->SideLengths[2] = 0.0;

  //double bounds_ROI[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, };
  //this->GetBoundsROI(bounds_ROI);

  //double origin_ROI[4] = { 0.0, 0.0, 0.0, 1.0 };
  //for (int i = 0; i < 3; ++i)
  //  {
  //  this->SideLengths[i] = bounds_ROI[2*i+1] - bounds_ROI[2*i];
  //  origin_ROI[i] = (bounds_ROI[2*i+1] + bounds_ROI[2*i])/2.0;
  //  }

  //double origin_World[4] = { 0.0, 0.0, 0.0, 0.0 };
  //this->ROIToWorldMatrix->MultiplyPoint(origin_ROI, origin_World);
  //for (int i = 0; i < 3; ++i)
  //  {
  //  this->ROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
  //  }

  //if (index >= 0 && position_World)
  //  {
  //  this->SetNthControlPointPositionWorldFromArray(index, position_World, positionStatus);
  //  }
}

#include <vtkSphere.h>
//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateSphereROIFromControlPoints()
{
  /*if (pointIndex >= 0 && position_World)
    {
    Superclass::SetNthControlPointPositionWorldFromArray(pointIndex, position_World, positionStatus);
    }

  vtkPoints* points = this->CurveInputPoly->GetPoints();

  float sphereDimensions[4] = { 0 };

  vtkSphere::ComputeBoundingSphere((float*)points->GetData()->GetVoidPointer(0), points->GetNumberOfPoints(), sphereDimensions, nullptr);
  this->SideLengths[0] = 2*sphereDimensions[3];
  this->SideLengths[1] = 2*sphereDimensions[3];
  this->SideLengths[2] = 2*sphereDimensions[3];

  double origin_Local[3] = { sphereDimensions[0], sphereDimensions[1], sphereDimensions[2] };
  double origin_World[3] = {0};
  this->TransformPointToWorld(origin_Local, origin_World);

  vtkNew<vtkMatrix4x4> newROIToWorldMatrix;
  for (int i = 0; i < 3; ++i)
    {
    newROIToWorldMatrix->SetElement(i, 3, origin_World[i]);
    }
  this->ROIToWorldMatrix->DeepCopy(newROIToWorldMatrix);*/
}
