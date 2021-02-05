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
#include <vtkPlanes.h>
#include <vtkPointData.h>
#include <vtkPointLocator.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkStringArray.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkTriangleFilter.h>

// STD includes
#include <sstream>

// vtkAddon includes
#include <vtkAddonMathUtilities.h>

const int NUMBER_OF_BOX_CONTROL_POINTS = 2; // 1 center; 1 corner; Used for initial ROI definition, then removed
const int NUMBER_OF_BOUNDING_BOX_CONTROL_POINTS = 1e6; // Any number of points

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROINode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROINode::vtkMRMLMarkupsROINode()
{
  this->PropertiesLabelText = "";

  //this->ROIType = BOX;
  //this->RequiredNumberOfControlPoints = NUMBER_OF_BOX_CONTROL_POINTS;

  this->ROIType = BOUNDING_BOX;
  this->RequiredNumberOfControlPoints = NUMBER_OF_BOUNDING_BOX_CONTROL_POINTS;

  //this->ROIType = SPHERE;
  //this->RequiredNumberOfControlPoints = NUMBER_OF_SPHERE_CONTROL_POINTS;

  this->SideLengths[0] = 0.0;
  this->SideLengths[1] = 0.0;
  this->SideLengths[2] = 0.0;

  this->IsUpdatingControlPointsFromROI = false;
  this->IsUpdatingROIFromControlPoints = false;
  this->ROIUpdatedTime = 0;

  this->CurveInputPoly->GetPoints()->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
  this->ROIToLocalMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  this->ROIToLocalMatrix->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);

  this->InteractionHandleToWorldMatrix->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);

  this->InsideOut = false;
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

  double axis_Local[3] = { 0.0, 0.0, 0.0 };
  this->GetAxisLocal(axisIndex, axis_Local);

  vtkNew<vtkGeneralTransform> localToWorldTransform;
  vtkMRMLTransformNode::GetTransformBetweenNodes(this->GetParentTransformNode(), nullptr, localToWorldTransform);
  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(origin_Local);
  localToWorldTransform->TransformVectorAtPoint(origin_Local, axis_Local, axis_World);
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

  double axis4_Local[4] = { 0.0, 0.0, 0.0, 0.0 };
  axis4_Local[axisIndex] = 1.0;
  this->ROIToLocalMatrix->MultiplyPoint(axis4_Local, axis_Local);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData)
{
  if (caller == this->CurveInputPoly->GetPoints() || caller == this->GetParentTransformNode())
    {
    // TODO: Update handles from parent transform
    this->UpdateROIFromControlPoints();
    }
  else if (caller == this->ROIToLocalMatrix.GetPointer() && event == vtkCommand::ModifiedEvent)
    {
    this->UpdateInteractionHandleToWorldMatrix();
    this->Modified();
    }
  else if (caller = this->InteractionHandleToWorldMatrix)
    {
    vtkNew<vtkMatrix4x4> worldToLocalTransform;
    if (this->GetParentTransformNode())
      {
      this->GetParentTransformNode()->GetMatrixTransformFromWorld(worldToLocalTransform);
      }

    vtkNew<vtkTransform> roiToLocalTransform;
    roiToLocalTransform->Concatenate(worldToLocalTransform);
    roiToLocalTransform->Concatenate(this->InteractionHandleToWorldMatrix);
    this->ROIToLocalMatrix->DeepCopy(roiToLocalTransform->GetMatrix());
    this->Modified();
    }
  Superclass::ProcessMRMLEvents(caller, event, callData);
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
    default:
      break;
    }
  this->UpdateROIFromControlPoints();
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetOrigin(double origin_Local[3])
{
  if (!origin_Local)
    {
    vtkErrorMacro("GetOrigin: Invalid origin argument");
    return;
    }

  double origin_Local4[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->ROIToLocalMatrix->MultiplyPoint(origin_Local4, origin_Local4);

  origin_Local[0] = origin_Local4[0];
  origin_Local[1] = origin_Local4[1];
  origin_Local[2] = origin_Local4[2];
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetOriginWorld(double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("GetOriginWorld: Invalid origin argument");
    return;
    }

  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(origin_Local);
  this->TransformPointToWorld(origin_Local, origin_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetOriginWorld(const double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("SetOriginWorld: Invalid origin argument");
    return;
    }

  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  this->TransformPointFromWorld(origin_World, origin_Local);
  this->SetOrigin(origin_Local);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetOrigin(const double origin_Local[3])
{
  if (!origin_Local)
    {
    vtkErrorMacro("SetOrigin: Invalid origin argument");
    return;
    }

  if (this->ROIToLocalMatrix->GetElement(0, 3) == origin_Local[0] &&
    this->ROIToLocalMatrix->GetElement(1, 3) == origin_Local[1] &&
    this->ROIToLocalMatrix->GetElement(2, 3) == origin_Local[2])
    {
    return;
    }

  vtkNew<vtkMatrix4x4> newROIToLocalMatrix;
  newROIToLocalMatrix->DeepCopy(this->ROIToLocalMatrix);
  for (int i = 0; i < 3; ++i)
    {
    newROIToLocalMatrix->SetElement(i, 3, origin_Local[i]);
    }

  MRMLNodeModifyBlocker blocker(this);
  this->ROIToLocalMatrix->DeepCopy(newROIToLocalMatrix);
  this->UpdateControlPointsFromROI();
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetSideLengths(const double sideLengths[3])
{
  this->SetSideLengths(sideLengths[0], sideLengths[1], sideLengths[2]);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::SetSideLengths(double x, double y, double z)
{
  if (this->SideLengths[0] == x && this->SideLengths[1] == y && this->SideLengths[2] == z)
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(this);
  this->SideLengths[0] = x;
  this->SideLengths[1] = y;
  this->SideLengths[2] = z;
  this->UpdateControlPointsFromROI();
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetBoundsROI(double bounds_ROI[6])
{
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
  if (this->IsUpdatingControlPointsFromROI || this->IsUpdatingROIFromControlPoints)
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

  this->UpdateBoundingBoxROIFromControlPoints();

  double newSideLengths[3] = { 0.0, 0.0, 0.0 };
  this->GetSideLengths(newSideLengths);

  double minimumSideLength = VTK_DOUBLE_MAX;
  for (int i = 0; i < 3; ++i)
    {
    if (newSideLengths[i] <= 0.0)
      {
      continue;
      }
    minimumSideLength = std::min(minimumSideLength, newSideLengths[i]);
    }
  if (minimumSideLength == VTK_DOUBLE_MAX)
    {
    minimumSideLength = 0.0;
    }

  if (this->GetNumberOfControlPoints() == NUMBER_OF_BOX_CONTROL_POINTS)
    {
    if (newSideLengths[0] == 0.0)
      {
      newSideLengths[0] = minimumSideLength;
      }
    if (newSideLengths[1] == 0.0)
      {
      newSideLengths[1] = minimumSideLength;
      }
    if (newSideLengths[2] == 0.0)
      {
      newSideLengths[2] = minimumSideLength;
      }
    }
  this->SetSideLengths(newSideLengths);

  if (this->GetNumberOfDefinedControlPoints() == NUMBER_OF_BOX_CONTROL_POINTS)
    {
    this->RequiredNumberOfControlPoints = 0;
    this->RemoveAllControlPoints();
    }
  else
    {
    this->RequiredNumberOfControlPoints = 2;
    }
}


//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateBoundingBoxROIFromControlPoints()
{
  MRMLNodeModifyBlocker blocker(this);

  double bounds_ROI[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, };
  int numberOfControlPoints = this->GetNumberOfControlPoints();
  if (numberOfControlPoints == 0)
    {
    for (int i = 0; i < 6; ++i)
      {
      bounds_ROI[i] = 0.0;
      }
    }

  vtkNew<vtkTransform> localToROITransform;
  localToROITransform->SetMatrix(this->ROIToLocalMatrix);
  localToROITransform->Inverse();

  for (int pointIndex = 0; pointIndex < this->GetNumberOfControlPoints(); ++pointIndex)
    {
    double point_Local[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPosition(pointIndex, point_Local);

    double point_ROI[3] = { 0.0, 0.0, 0.0 };
    localToROITransform->TransformPoint(point_Local, point_ROI);
    for (int i = 0; i < 3; ++i)
      {
      bounds_ROI[2 * i] = std::min(bounds_ROI[2 * i], point_ROI[i]);
      bounds_ROI[2 * i + 1] = std::max(bounds_ROI[2 * i + 1], point_ROI[i]);
      }
    }

  double origin_ROI[4] = { 0.0, 0.0, 0.0, 1.0 };
  double newSideLengths[3] = { 0.0, 0.0, 0.0 };
  for (int i = 0; i < 3; ++i)
    {
    newSideLengths[i] = bounds_ROI[2 * i + 1] - bounds_ROI[2 * i];
    origin_ROI[i] = (bounds_ROI[2 * i + 1] + bounds_ROI[2 * i]) / 2.0;
    }

  this->SetSideLengths(newSideLengths);

  double origin_Local[4] = { 0.0, 0.0, 0.0, 0.0 };
  this->ROIToLocalMatrix->MultiplyPoint(origin_ROI, origin_Local);
  this->SetOrigin(origin_Local);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromROI(int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  if (this->IsUpdatingControlPointsFromROI || this->IsUpdatingROIFromControlPoints)
    {
    return;
    }

  this->IsUpdatingControlPointsFromROI = true;
    {
      MRMLNodeModifyBlocker blocker(this);

      switch (this->ROIType)
        {
        case BOUNDING_BOX:
          this->UpdateControlPointsFromBoundingBoxROI(positionStatus);
          break;
        default:
          break;
        }
    }
  this->IsUpdatingControlPointsFromROI = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateControlPointsFromBoundingBoxROI(int positionStatus/*=vtkMRMLMarkupsNode::PositionDefined*/)
{
  vtkNew<vtkPoints> newROIPoints_World;

  vtkNew<vtkTransform> localToROITransform;
  localToROITransform->SetMatrix(this->ROIToLocalMatrix);
  localToROITransform->Inverse();

  double bounds_ROI[6] = { VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN, VTK_DOUBLE_MAX, VTK_DOUBLE_MIN };
  for (int pointIndex = 0; pointIndex < this->GetNumberOfControlPoints(); ++pointIndex)
    {
    double point_Local[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPosition(pointIndex, point_Local);

    double point_ROI[3] = { 0.0, 0.0, 0.0 };
    localToROITransform->TransformPoint(point_Local, point_ROI);
    for (int i = 0; i < 3; ++i)
      {
      bounds_ROI[2 * i] = std::min(bounds_ROI[2 * i], point_ROI[i]);
      bounds_ROI[2 * i + 1] = std::max(bounds_ROI[2 * i + 1], point_ROI[i]);
      }
    }
  if (this->GetNumberOfControlPoints() == 0)
    {
    for (int i = 0; i < 6; ++i)
      {
      bounds_ROI[i] = 0.0;
      }
    }

  double scale_ROI[3] = { 1.0, 1.0, 1.0 };
  double translation_ROI[3] = { 0.0, 0.0, 0.0 };
  for (int i = 0; i < 3; ++i)
    {
    double oldSideLength = bounds_ROI[2 * i + 1] - bounds_ROI[2 * i];
    scale_ROI[i] = this->SideLengths[i] / oldSideLength;
    translation_ROI[i] = -(bounds_ROI[2 * i + 1] + bounds_ROI[2 * i]) / 2.0;
    }

  vtkNew<vtkTransform> localToScaledLocalTransform;
  localToScaledLocalTransform->PostMultiply();
  localToScaledLocalTransform->Concatenate(localToROITransform);
  localToScaledLocalTransform->Translate(translation_ROI);
  localToScaledLocalTransform->Scale(scale_ROI);
  localToScaledLocalTransform->Concatenate(this->ROIToLocalMatrix);

  vtkNew<vtkTransformPolyDataFilter> localToScaledLocalTransformFilter;
  localToScaledLocalTransformFilter->SetTransform(localToScaledLocalTransform);
  localToScaledLocalTransformFilter->SetInputData(this->CurveInputPoly);

  vtkNew<vtkGeneralTransform> localToWorldTransform;
  if (this->GetParentTransformNode())
    {
    this->GetParentTransformNode()->GetTransformToWorld(localToWorldTransform);
    }

  vtkNew<vtkTransformPolyDataFilter> localToWorldTransformFilter;
  localToWorldTransformFilter->SetTransform(localToWorldTransform);
  localToWorldTransformFilter->SetInputConnection(localToScaledLocalTransformFilter->GetOutputPort());
  localToWorldTransformFilter->Update();

  this->SetControlPointPositionsWorld(localToWorldTransformFilter->GetOutput()->GetPoints());
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::GetTransformedPlanes(vtkPlanes* planes)
{
  if (!planes)
    {
    vtkErrorMacro("GetTransformedPlanes: Invalid planes");
    return;
    }

  vtkNew<vtkTransform> roiToWorldTransform;
  roiToWorldTransform->SetMatrix(this->ROIToLocalMatrix);

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  roiToWorldTransform->TransformPoint(origin_World, origin_World);

  vtkNew<vtkDoubleArray> normals;
  normals->SetNumberOfComponents(3);

  vtkNew<vtkPoints> points;

  {
    double lNormal_World[3] = { -1.0, 0.0, 0.0 };
    roiToWorldTransform->TransformVector(lNormal_World, lNormal_World);
    vtkMath::MultiplyScalar(lNormal_World, 0.5 * this->SideLengths[0]);
    double lOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(origin_World, lNormal_World, lOrigin_World);
    points->InsertNextPoint(lOrigin_World[0], lOrigin_World[1], lOrigin_World[2]);
    normals->InsertNextTuple3(lNormal_World[0], lNormal_World[1], lNormal_World[2]);
  }

  {
    double rNormal_World[3] = { 1.0, 0.0, 0.0 };
    roiToWorldTransform->TransformVector(rNormal_World, rNormal_World);
    vtkMath::MultiplyScalar(rNormal_World, 0.5 * this->SideLengths[0]);
    double rOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(origin_World, rNormal_World, rOrigin_World);
    points->InsertNextPoint(rOrigin_World[0], rOrigin_World[1], rOrigin_World[2]);
    normals->InsertNextTuple3(rNormal_World[0], rNormal_World[1], rNormal_World[2]);
  }

  {
    double pNormal_World[3] = { 0.0, -1.0, 0.0 };
    roiToWorldTransform->TransformVector(pNormal_World, pNormal_World);
    vtkMath::MultiplyScalar(pNormal_World, 0.5 * this->SideLengths[1]);
    double pOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(origin_World, pNormal_World, pOrigin_World);
    points->InsertNextPoint(pOrigin_World[0], pOrigin_World[1], pOrigin_World[2]);
    normals->InsertNextTuple3(pNormal_World[0], pNormal_World[1], pNormal_World[2]);
  }

  {
    double aNormal_World[3] = { 0.0, 1.0, 0.0 };
    roiToWorldTransform->TransformVector(aNormal_World, aNormal_World);
    vtkMath::MultiplyScalar(aNormal_World, 0.5 * this->SideLengths[1]);
    double aOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(origin_World, aNormal_World, aOrigin_World);
    points->InsertNextPoint(aOrigin_World[0], aOrigin_World[1], aOrigin_World[2]);
    normals->InsertNextTuple3(aNormal_World[0], aNormal_World[1], aNormal_World[2]);
  }

  {
    double iNormal_World[3] = { 0.0, 0.0, -1.0 };
    roiToWorldTransform->TransformVector(iNormal_World, iNormal_World);
    vtkMath::MultiplyScalar(iNormal_World, 0.5 * this->SideLengths[2]);
    double iOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(origin_World, iNormal_World, iOrigin_World);
    points->InsertNextPoint(iOrigin_World[0], iOrigin_World[1], iOrigin_World[2]);
    normals->InsertNextTuple3(iNormal_World[0], iNormal_World[1], iNormal_World[2]);
  }

  {
    double sNormal_World[3] = { 0.0, 0.0, 1.0 };
    roiToWorldTransform->TransformVector(sNormal_World, sNormal_World);
    vtkMath::MultiplyScalar(sNormal_World, 0.5 * this->SideLengths[2]);
    double sOrigin_World[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(origin_World, sNormal_World, sOrigin_World);
    points->InsertNextPoint(sOrigin_World[0], sOrigin_World[1], sOrigin_World[2]);
    normals->InsertNextTuple3(sNormal_World[0], sNormal_World[1], sNormal_World[2]);
  }

  if (this->InsideOut)
    {
    for (int i = 0; i < normals->GetNumberOfTuples(); ++i)
      {
      double* normal = normals->GetTuple3(i);
      normals->SetTuple3(i, -normal[0], -normal[1], -normal[2]);
      }
    }
  planes->SetNormals(normals);
  planes->SetPoints(points);

  vtkNew<vtkGeneralTransform> localToWorldTransform;
  vtkMRMLTransformNode::GetTransformBetweenNodes(this->GetParentTransformNode(), nullptr, localToWorldTransform);
  planes->SetTransform(localToWorldTransform);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsROINode::UpdateInteractionHandleToWorldMatrix()
{
  vtkNew<vtkMatrix4x4> localToWorldMatrix;
  this->GetParentTransformNode()->GetMatrixTransformBetweenNodes(this->GetParentTransformNode(), nullptr, localToWorldMatrix);

  vtkNew<vtkTransform> handleToWorldMatrix;
  handleToWorldMatrix->Concatenate(localToWorldMatrix);
  handleToWorldMatrix->Concatenate(this->ROIToLocalMatrix);
  this->InteractionHandleToWorldMatrix->DeepCopy(handleToWorldMatrix->GetMatrix());
}
