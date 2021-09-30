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

// MRML includes
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsPlaneDisplayNode.h"
#include "vtkMRMLMarkupsPlaneNode.h"
#include "vtkMRMLMeasurementArea.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLStorageNode.h"
#include "vtkMRMLTransformNode.h"

// vtkAddon includes
#include <vtkAddonMathUtilities.h>

// VTK includes
#include <vtkCallbackCommand.h>
#include <vtkCollection.h>
#include <vtkGeneralTransform.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlane.h>
#include <vtkTransform.h>
#include <vtkTriangle.h>

// STD includes
#include <sstream>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsPlaneNode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsPlaneNode::vtkMRMLMarkupsPlaneNode()
{
  this->RequiredNumberOfControlPoints = 2;
  this->MaximumNumberOfControlPoints = 2;

  this->PlaneType = PlaneTypePointNormal;
  //this->RequiredNumberOfControlPoints = 3;
  //this->MaximumNumberOfControlPoints = 1e6;
  //this->PlaneType = PlaneTypePlaneFit;
  this->SizeMode = SizeModeAuto;

  this->ObjectToBaseMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  this->BaseToNodeMatrix = vtkSmartPointer<vtkMatrix4x4>::New();

  this->CurveInputPoly->GetPoints()->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
  this->ObjectToBaseMatrix->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
  this->BaseToNodeMatrix->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);
  this->InteractionHandleToWorldMatrix->AddObserver(vtkCommand::ModifiedEvent, this->MRMLCallbackCommand);

  // Setup measurements calculated for this markup type
  vtkNew<vtkMRMLMeasurementArea> areaMeasurement;
  areaMeasurement->SetEnabled(false);
  areaMeasurement->SetName("area");
  areaMeasurement->SetInputMRMLNode(this);
  this->Measurements->AddItem(areaMeasurement);
}

//----------------------------------------------------------------------------
vtkMRMLMarkupsPlaneNode::~vtkMRMLMarkupsPlaneNode() = default;

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::WriteXML(ostream& of, int nIndent)
{
  Superclass::WriteXML(of,nIndent);
  vtkMRMLWriteXMLBeginMacro(of);
  vtkMRMLWriteXMLEnumMacro(sizeMode, SizeMode);
  vtkMRMLWriteXMLVectorMacro(size, Size, double, 2);
  vtkMRMLWriteXMLFloatMacro(autoSizeScalingFactor, AutoSizeScalingFactor);
  vtkMRMLWriteXMLMatrix4x4Macro(objectToBaseMatrix, ObjectToBaseMatrix);
  vtkMRMLWriteXMLEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::ReadXMLAttributes(const char** atts)
{
  MRMLNodeModifyBlocker blocker(this);
  Superclass::ReadXMLAttributes(atts);
  vtkMRMLReadXMLBeginMacro(atts);
  vtkMRMLReadXMLEnumMacro(sizeMode, SizeMode);
  vtkMRMLReadXMLVectorMacro(size, Size, double, 2);
  vtkMRMLReadXMLFloatMacro(autoSizeScalingFactor, AutoSizeScalingFactor);
  vtkMRMLReadXMLOwnedMatrix4x4Macro(planeTobaseMatrix, ObjectToBaseMatrix); // Backwards compatible with old name
  vtkMRMLReadXMLOwnedMatrix4x4Macro(objectToBaseMatrix, ObjectToBaseMatrix);
  vtkMRMLReadXMLEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::CopyContent(vtkMRMLNode* anode, bool deepCopy/*=true*/)
{
  MRMLNodeModifyBlocker blocker(this);
  Superclass::CopyContent(anode, deepCopy);
  vtkMRMLCopyBeginMacro(anode);
  vtkMRMLCopyEnumMacro(SizeMode);
  vtkMRMLCopyVectorMacro(Size, double, 2);
  vtkMRMLCopyFloatMacro(AutoSizeScalingFactor);
  vtkMRMLCopyOwnedMatrix4x4Macro(ObjectToBaseMatrix);
  vtkMRMLCopyEndMacro();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os,indent);
  vtkMRMLPrintBeginMacro(os, indent);
  vtkMRMLPrintEnumMacro(SizeMode);
  vtkMRMLPrintVectorMacro(Size, double, 2);
  vtkMRMLPrintFloatMacro(AutoSizeScalingFactor);
  vtkMRMLPrintMatrix4x4Macro(ObjectToBaseMatrix);
  vtkMRMLPrintEndMacro();
}

//---------------------------------------------------------------------------
int vtkMRMLMarkupsPlaneNode::EndModify(int previousDisableModifiedEventState)
{
  //bool processPendingPointModifiedEvents = !previousDisableModifiedEventState &&P
  //  (this->GetModifiedEventPending() > 0
  //    || this->GetCustomModifiedEventPending(vtkMRMLMarkupsNode::PointModifiedEvent) > 0
  //    || this->GetCustomModifiedEventPending(vtkMRMLMarkupsNode::PointAddedEvent) > 0
  //    || this->GetCustomModifiedEventPending(vtkMRMLMarkupsNode::PointRemovedEvent) > 0
  //    || this->GetCustomModifiedEventPending(vtkMRMLMarkupsNode::PointPositionDefinedEvent) > 0
  //    || this->GetCustomModifiedEventPending(vtkMRMLMarkupsNode::PointPositionUndefinedEvent) > 0);
  //if (processPendingPointModifiedEvents)
  //  {
  //  this->UpdatePlaneFromControlPoints();
  //  }

  return Superclass::EndModify(previousDisableModifiedEventState);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::ApplyTransform(vtkAbstractTransform* transform)
{
  if (!transform)
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(this);

  bool wasUpdatingControlPointsFromPlane = this->IsUpdatingControlPointsFromPlane;
  this->IsUpdatingControlPointsFromPlane = true;

  bool wasUpdatingPlaneFromControlPoints = this->IsUpdatingPlaneFromControlPoints;
  this->IsUpdatingPlaneFromControlPoints = true;

  vtkNew<vtkMatrix4x4> oldBaseToNodeMatrix;
  oldBaseToNodeMatrix->DeepCopy(this->BaseToNodeMatrix);

  Superclass::ApplyTransform(transform);

  vtkNew<vtkMatrix4x4> newObjectToNodeMatrix;
  this->GenerateOrthogonalMatrix(oldBaseToNodeMatrix, newObjectToNodeMatrix, transform, false);
  this->BaseToNodeMatrix->DeepCopy(newObjectToNodeMatrix);

  double xAxis_Node_New[3] = { 1.0, 0.0, 0.0 };
  double yAxis_Node_New[3] = { 0.0, 1.0, 0.0 };
  double zAxis_Node_New[3] = { 0.0, 0.0, 1.0 };
  double center_Node_New[3] = { 0.0, 0.0, 0.0 };
  this->GetAxes(xAxis_Node_New, yAxis_Node_New, zAxis_Node_New);
  this->GetCenter(center_Node_New);

  // Update size by calculating diffference in scaling between transformed/untransformed axes
  vtkAbstractTransform* transformInverse = transform->GetInverse();
  this->Size[0] /= vtkMath::Norm(transformInverse->TransformVectorAtPoint(center_Node_New, xAxis_Node_New));
  this->Size[1] /= vtkMath::Norm(transformInverse->TransformVectorAtPoint(center_Node_New, yAxis_Node_New));

  this->IsUpdatingControlPointsFromPlane = wasUpdatingControlPointsFromPlane;
  this->IsUpdatingPlaneFromControlPoints = wasUpdatingPlaneFromControlPoints;

  this->Modified();
}

//----------------------------------------------------------------------------
const char* vtkMRMLMarkupsPlaneNode::GetPlaneTypeAsString(int planeType)
{
  switch (planeType)
    {
    case vtkMRMLMarkupsPlaneNode::PlaneType3Points:
      return "threePoints";
    case vtkMRMLMarkupsPlaneNode::PlaneTypePointNormal:
      return "pointNormal";
    case vtkMRMLMarkupsPlaneNode::PlaneTypePlaneFit:
      return "planeFit";
    default:
    break;
    }
  return "";
}

//----------------------------------------------------------------------------
int vtkMRMLMarkupsPlaneNode::GetPlaneTypeFromString(const char* planeType)
{
  if (planeType == nullptr)
    {
    // invalid name
    return -1;
    }
  for (int i = 0; i < vtkMRMLMarkupsPlaneNode::PlaneType_Last; i++)
    {
    if (strcmp(planeType, vtkMRMLMarkupsPlaneNode::GetPlaneTypeAsString(i)) == 0)
      {
      // found a matching name
      return i;
      }
    }
  // unknown plane type
  return -1;
}

//----------------------------------------------------------------------------
const char* vtkMRMLMarkupsPlaneNode::GetSizeModeAsString(int sizeMode)
{
  switch (sizeMode)
    {
  case SizeModeAuto:
    return "auto";
  case SizeModeAbsolute:
    return "absolute";
  default:
    break;
    }
  return "";
}

//----------------------------------------------------------------------------
int vtkMRMLMarkupsPlaneNode::GetSizeModeFromString(const char* sizeMode)
{
  if (sizeMode == nullptr)
    {
    // invalid size mode
    return -1;
    }
  for (int i = 0; i < SizeMode_Last; ++i)
    {
    if (strcmp(vtkMRMLMarkupsPlaneNode::GetSizeModeAsString(i), sizeMode) == 0)
      {
      return i;
      }
    }
  // unknown size mode
  return -1;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetPlaneType(int planeType)
{
  if (this->PlaneType == planeType)
    {
    return;
    }

  this->PlaneType = planeType;
  switch (planeType)
    {
    case vtkMRMLMarkupsPlaneNode::PlaneType3Points:
      this->RequiredNumberOfControlPoints = 3;
      this->MaximumNumberOfControlPoints = 3;
      break;
    case vtkMRMLMarkupsPlaneNode::PlaneTypePointNormal:
      this->RequiredNumberOfControlPoints = 2;
      this->MaximumNumberOfControlPoints = 2;
      break;
    case vtkMRMLMarkupsPlaneNode::PlaneTypePlaneFit:
      this->RequiredNumberOfControlPoints = 3;
      this->MaximumNumberOfControlPoints = 1e6;
    default:
      break;
    }

  MRMLNodeModifyBlocker blocker(this);
  this->UpdatePlaneFromControlPoints();
  this->UpdateControlPointsFromPlane();
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetNormal(double normal[3])
{
  if (!normal)
    {
    vtkErrorMacro("GetNormal: Invalid normal argument");
    return;
    }

  double x[3] = { 0 };
  double y[3] = { 0 };
  this->GetAxes(x, y, normal);
}

//----------------------------------------------------------------------------
double* vtkMRMLMarkupsPlaneNode::GetNormal()
{
  this->GetNormal(this->Normal);
  return this->Normal;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetNormalWorld(double normalWorld[3])
{
  if (!normalWorld)
    {
    vtkErrorMacro("GetNormalWorld: Invalid normal argument");
    return;
    }

  double x[3] = { 0 };
  double y[3] = { 0 };
  this->GetAxesWorld(x, y, normalWorld);
}

//----------------------------------------------------------------------------
double* vtkMRMLMarkupsPlaneNode::GetNormalWorld()
{
  this->GetNormalWorld(this->NormalWorld);
  return this->NormalWorld;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetNormal(const double normal_Node[3])
{
  if (!normal_Node)
    {
    vtkErrorMacro("SetNormal: Invalid normal argument");
    return;
    }

  MRMLNodeModifyBlocker blocker(this);

  double newNormal_Node[3] = { normal_Node[0], normal_Node[1], normal_Node[2] };
  vtkMath::Normalize(newNormal_Node);

  double currentNormal_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetNormal(currentNormal_Node);

  double epsilon = 0.0001;
  if (vtkMath::Dot(newNormal_Node, currentNormal_Node) >= 1.0 - epsilon)
    {
    // Normal vectors are equivalent, no change required.
    return;
    }

  double angleRadians = vtkMath::AngleBetweenVectors(currentNormal_Node, newNormal_Node);
  double rotationAxis_Node[3] = { 0.0, 0.0, 0.0 };
  vtkMath::Cross(currentNormal_Node, newNormal_Node, rotationAxis_Node);
  if (vtkMath::Norm(rotationAxis_Node) < epsilon)
    {
    // New + old normals are facing opposite directions.
    // Find a perpendicular axis to flip around.
    vtkMath::Perpendiculars(currentNormal_Node, rotationAxis_Node, nullptr, 0);
    }

  vtkNew<vtkTransform> oldToNewNormalTransform;
  double origin_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(origin_Node);
  oldToNewNormalTransform->Translate(origin_Node);
  oldToNewNormalTransform->RotateWXYZ(vtkMath::DegreesFromRadians(angleRadians), rotationAxis_Node);
  vtkMath::MultiplyScalar(origin_Node, -1.0);
  oldToNewNormalTransform->Translate(origin_Node);
  this->ApplyTransform(oldToNewNormalTransform);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetNormal(double x_Node, double y_Node, double z_Node)
{
  double normal_Node[3] = { x_Node, y_Node, z_Node };
  this->SetNormal(normal_Node);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetNormalWorld(const double normal_World[3])
{
  double normal_Node[3] = { normal_World[0], normal_World[1], normal_World[2] };

  vtkMRMLTransformNode* transformNode = this->GetParentTransformNode();
  if (transformNode)
    {
    // Get transform
    vtkNew<vtkGeneralTransform> transformToWorld;
    transformNode->GetTransformFromWorld(transformToWorld);

    // Convert coordinates
    double origin_World[3] = { 0 };
    this->GetOriginWorld(origin_World);
    transformToWorld->TransformVectorAtPoint(origin_World, normal_World, normal_Node);
    }
  this->SetNormal(normal_Node);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetNormalWorld(double x_World, double y_World, double z_World)
{
  double normal_World[3] = { x_World, y_World, z_World };
  this->SetNormalWorld(normal_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetOrigin(double origin_Node[3])
{
  if (!origin_Node)
    {
    vtkErrorMacro("GetOrigin: Invalid origin argument");
    return;
    }

  origin_Node[0] = 0.0;
  origin_Node[1] = 0.0;
  origin_Node[2] = 0.0;

  double origin_Object[3] = { 0.0, 0.0, 0.0 };

  vtkNew<vtkMatrix4x4> objectToNodeMatrix;
  this->GetObjectToNodeMatrix(objectToNodeMatrix);

  vtkNew<vtkTransform> objectToNodeTransform;
  objectToNodeTransform->SetMatrix(objectToNodeMatrix);
  objectToNodeTransform->TransformPoint(origin_Object, origin_Node);
}

//----------------------------------------------------------------------------
double* vtkMRMLMarkupsPlaneNode::GetOrigin()
{
  vtkNew<vtkMatrix4x4> objectToNodeMatrix;
  this->GetObjectToNodeMatrix(objectToNodeMatrix);

  vtkNew<vtkTransform> objectToNodeTransform;
  objectToNodeTransform->SetMatrix(objectToNodeMatrix);
  double originObject[3] = { 0.0, 0.0, 0.0 };
  objectToNodeTransform->TransformPoint(originObject, this->Origin);
  return this->Origin;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetOriginWorld(double origin_World[3])
{
  if (!origin_World)
    {
    vtkErrorMacro("GetOriginWorld: Invalid origin argument");
    return;
    }

  double origin_Object[3] = { 0.0, 0.0, 0.0 };

  vtkNew<vtkMatrix4x4> objectToWorldMatrix;
  this->GetObjectToWorldMatrix(objectToWorldMatrix);

  vtkNew<vtkTransform> objectToWorldTransform;
  objectToWorldTransform->SetMatrix(objectToWorldMatrix);
  objectToWorldTransform->TransformPoint(origin_Object, origin_World);
}

//----------------------------------------------------------------------------
double* vtkMRMLMarkupsPlaneNode::GetOriginWorld()
{
  vtkNew<vtkMatrix4x4> objectToWorldMatrix;
  this->GetObjectToWorldMatrix(objectToWorldMatrix);

  vtkNew<vtkTransform> objectToWorldTransform;
  objectToWorldTransform->SetMatrix(objectToWorldMatrix);
  double originObject[3] = { 0.0, 0.0, 0.0 };
  objectToWorldTransform->TransformPoint(originObject, this->OriginWorld);
  return this->OriginWorld;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetOrigin(const double origin_Node[3])
{
  if (!origin_Node)
    {
    vtkWarningMacro("SetOrigin: Invalid origin argument");
    return;
    }

  MRMLNodeModifyBlocker blocker(this);
  if (this->GetNumberOfControlPoints() < 1)
    {
    this->AddNControlPoints(1);
    }

  double previousOrigin_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(previousOrigin_Node);

  double displacementVector_Node[3] = { 0.0 };
  vtkMath::Subtract(origin_Node, previousOrigin_Node, displacementVector_Node);

  //vtkNew<vtkTransform> oldToNewOriginTransform;
  //oldToNewOriginTransform->Translate(displacementVector_Node);
  //this->ApplyTransform(oldToNewOriginTransform);

  vtkNew<vtkMatrix4x4> newBaseToNodeMatrix;
  newBaseToNodeMatrix->DeepCopy(this->BaseToNodeMatrix);
  newBaseToNodeMatrix->SetElement(0, 3, origin_Node[0]);
  newBaseToNodeMatrix->SetElement(1, 3, origin_Node[1]);
  newBaseToNodeMatrix->SetElement(2, 3, origin_Node[2]);
  this->BaseToNodeMatrix->DeepCopy(newBaseToNodeMatrix);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetOrigin(double x_Node, double y_Node, double z_Node)
{
  double origin_Node[3] = { x_Node, y_Node, z_Node };
  this->SetOrigin(origin_Node);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetOriginWorld(const double origin_World[3])
{
  double origin_Node[3] = { 0.0 };
  this->TransformPointFromWorld(origin_World, origin_Node);
  this->SetOrigin(origin_Node);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetOriginWorld(double x_World, double y_World, double z_World)
{
  double origin_World[3] = { x_World, y_World, z_World };
  this->SetOrigin(origin_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::CalculateAxesFromPoints(const double point0[3], const double point1[3], const double point2[3],
  double x[3], double y[3], double z[3])
{
  vtkMath::Subtract(point1, point0, x);
  vtkMath::Normalize(x);

  double tempVector[3] = { 0.0 };
  vtkMath::Subtract(point2, point0, tempVector);
  vtkMath::Cross(x, tempVector, z);
  vtkMath::Normalize(z);

  vtkMath::Cross(z, x, y);
  vtkMath::Normalize(y);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetBaseToNodeMatrix(vtkMatrix4x4* baseToNodeMatrix)
{
  if (!baseToNodeMatrix)
    {
    vtkErrorMacro(<< "GetObjectToNodeMatrix: Invalid objectToNodeMatrix");
    return;
    }

  baseToNodeMatrix->DeepCopy(this->BaseToNodeMatrix);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetObjectToNodeMatrix(vtkMatrix4x4* objectToNodeMatrix)
{
  if (!objectToNodeMatrix)
    {
    vtkErrorMacro(<< "GetObjectToNodeMatrix: Invalid objectToNodeMatrix");
    return;
    }

  if (!this->GetIsPlaneValid())
    {
    objectToNodeMatrix->Identity();
    return;
    }

  vtkNew<vtkMatrix4x4> baseToNodeMatrix;
  this->GetBaseToNodeMatrix(baseToNodeMatrix);

  vtkNew<vtkTransform> objectToNodeTransform;
  objectToNodeTransform->PostMultiply();
  objectToNodeTransform->Concatenate(this->ObjectToBaseMatrix);
  objectToNodeTransform->Concatenate(baseToNodeMatrix);
  objectToNodeMatrix->DeepCopy(objectToNodeTransform->GetMatrix());
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetObjectToWorldMatrix(vtkMatrix4x4* objectToWorldMatrix)
{
  if (!objectToWorldMatrix)
    {
    return;
    }

  vtkNew<vtkTransform> objectToWorldTransform;
  objectToWorldTransform->PostMultiply();
  objectToWorldTransform->Concatenate(this->ObjectToBaseMatrix);
  objectToWorldTransform->Concatenate(this->BaseToNodeMatrix);
  if (this->GetParentTransformNode())
    {
    vtkNew<vtkGeneralTransform> transformToWorld;
    this->GetParentTransformNode()->GetTransformToWorld(transformToWorld);
    this->GenerateOrthogonalMatrix(objectToWorldTransform->GetMatrix(), objectToWorldMatrix, transformToWorld);
    }
  else
    {
    objectToWorldMatrix->DeepCopy(objectToWorldTransform->GetMatrix());
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetAxes(double xAxis_Node[3], double yAxis_Node[3], double zAxis_Node[3])
{
  if (!xAxis_Node || !yAxis_Node || !zAxis_Node)
    {
    vtkErrorMacro("GetAxes: Invalid input argument");
    return;
    }

  for (int i = 0; i < 3; ++i)
    {
    xAxis_Node[i] = 0.0;
    yAxis_Node[i] = 0.0;
    zAxis_Node[i] = 0.0;
    }
  xAxis_Node[0] = 1.0;
  yAxis_Node[1] = 1.0;
  zAxis_Node[2] = 1.0;

  double xAxis_Object[3] = { 1.0, 0.0, 0.0 };
  double yAxis_Object[3] = { 0.0, 1.0, 0.0 };
  double zAxis_Object[3] = { 0.0, 0.0, 1.0 };

  vtkNew<vtkMatrix4x4> objectToNodeMatrix;
  this->GetObjectToNodeMatrix(objectToNodeMatrix);

  vtkNew<vtkTransform> objectToNodeTransform;
  objectToNodeTransform->SetMatrix(objectToNodeMatrix);
  objectToNodeTransform->TransformVector(xAxis_Object, xAxis_Node);
  objectToNodeTransform->TransformVector(yAxis_Object, yAxis_Node);
  objectToNodeTransform->TransformVector(zAxis_Object, zAxis_Node);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetAxesWorld(double xAxis_World[3], double yAxis_World[3], double zAxis_World[3])
{
  if (!xAxis_World || !yAxis_World || !zAxis_World)
    {
    vtkErrorMacro("GetAxesWorld: Invalid input argument");
    return;
    }

  for (int i = 0; i < 3; ++i)
    {
    xAxis_World[i] = 0.0;
    yAxis_World[i] = 0.0;
    zAxis_World[i] = 0.0;
    }
  xAxis_World[0] = 1.0;
  yAxis_World[1] = 1.0;
  zAxis_World[2] = 1.0;

  double xAxis_Object[3] = { 1.0, 0.0, 0.0 };
  double yAxis_Object[3] = { 0.0, 1.0, 0.0 };
  double zAxis_Object[3] = { 0.0, 0.0, 1.0 };

  vtkNew<vtkMatrix4x4> objectToWorldMatrix;
  this->GetObjectToWorldMatrix(objectToWorldMatrix);

  vtkNew<vtkTransform> objectToWorldTransform;
  objectToWorldTransform->SetMatrix(objectToWorldMatrix);
  objectToWorldTransform->TransformVector(xAxis_Object, xAxis_World);
  objectToWorldTransform->TransformVector(yAxis_Object, yAxis_World);
  objectToWorldTransform->TransformVector(zAxis_Object, zAxis_World);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetAxes(const double xAxis_Node[3], const double yAxis_Node[3], const double zAxis_Node[3])
{
  if (!xAxis_Node || !yAxis_Node || !zAxis_Node)
    {
    vtkErrorMacro("SetAxes: Invalid input axes");
    return;
    }

  double epsilon = 1e-5;
  double tempX[3] = { 0.0 };
  double tempY[3] = { 0.0 };
  double tempZ[3] = { 0.0 };
  vtkMath::Cross(yAxis_Node, zAxis_Node, tempX);
  vtkMath::Cross(zAxis_Node, xAxis_Node, tempY);
  vtkMath::Cross(xAxis_Node, yAxis_Node, tempZ);
  if (vtkMath::Dot(tempX, xAxis_Node) <= 1.0 - epsilon ||
      vtkMath::Dot(tempY, yAxis_Node) <= 1.0 - epsilon ||
      vtkMath::Dot(tempZ, zAxis_Node) <= 1.0 - epsilon)
    {
    vtkErrorMacro("SetAxes: Invalid direction vectors!");
    return;
    }

  if (vtkMath::Dot(xAxis_Node, yAxis_Node) >= epsilon ||
      vtkMath::Dot(yAxis_Node, zAxis_Node) >= epsilon ||
      vtkMath::Dot(zAxis_Node, xAxis_Node) >= epsilon)
    {
    vtkErrorMacro("SetAxes: Invalid vectors");
    }

  MRMLNodeModifyBlocker blocker(this);
  this->CreatePlane();

  double previousXAxis_Node[3] = { 0.0 };
  double previousYAxis_Node[3] = { 0.0 };
  double previousZAxis_Node[3] = { 0.0 };
  this->GetAxes(previousXAxis_Node, previousYAxis_Node, previousZAxis_Node);

  vtkNew<vtkMatrix4x4> previousAxisToIdentity;
  for (int i = 0; i < 3; ++i)
    {
    previousAxisToIdentity->SetElement(i, 0, previousXAxis_Node[i]);
    previousAxisToIdentity->SetElement(i, 1, previousYAxis_Node[i]);
    previousAxisToIdentity->SetElement(i, 2, previousZAxis_Node[i]);
    }
  previousAxisToIdentity->Invert();

  vtkNew<vtkMatrix4x4> identityToNewAxis;
  for (int i = 0; i < 3; ++i)
    {
    identityToNewAxis->SetElement(i, 0, xAxis_Node[i]);
    identityToNewAxis->SetElement(i, 1, yAxis_Node[i]);
    identityToNewAxis->SetElement(i, 2, zAxis_Node[i]);
    }

  double origin_Node[3] = { 0 };
  this->GetOrigin(origin_Node);

  vtkNew<vtkTransform> oldToNewAxesTransform;
  oldToNewAxesTransform->PostMultiply();
  vtkMath::MultiplyScalar(origin_Node, -1);
  oldToNewAxesTransform->Translate(origin_Node);
  oldToNewAxesTransform->Concatenate(previousAxisToIdentity);
  oldToNewAxesTransform->Concatenate(identityToNewAxis);
  vtkMath::MultiplyScalar(origin_Node, -1);
  oldToNewAxesTransform->Translate(origin_Node);

  this->ApplyTransform(oldToNewAxesTransform);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetAxesWorld(const double xAxis_World[3], const double yAxis_World[3], const double zAxis_World[3])
{
  double xAxis_Node[3] = { xAxis_World[0], xAxis_World[1], xAxis_World[2] };
  double yAxis_Node[3] = { yAxis_World[0], yAxis_World[1], yAxis_World[2] };
  double zAxis_Node[3] = { zAxis_World[0], zAxis_World[1], zAxis_World[2] };

  MRMLNodeModifyBlocker blocker(this);
  this->CreatePlane();

  vtkMRMLTransformNode* transformNode = this->GetParentTransformNode();
  if (transformNode)
    {
    // Get transform
    vtkNew<vtkGeneralTransform> worldToNodeTransform;
    transformNode->GetTransformFromWorld(worldToNodeTransform.GetPointer());

    // Convert coordinates
    double origin_World[3] = { 0 };
    this->GetOriginWorld(origin_World);
    worldToNodeTransform->TransformVectorAtPoint(origin_World, xAxis_World, xAxis_Node);
    worldToNodeTransform->TransformVectorAtPoint(origin_World, yAxis_World, yAxis_Node);
    worldToNodeTransform->TransformVectorAtPoint(origin_World, zAxis_World, zAxis_Node);
    }
  this->SetAxes(xAxis_Node, yAxis_Node, zAxis_Node);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::ProcessMRMLEvents(vtkObject* caller, unsigned long event, void* callData)
{
  if (caller == this->CurveInputPoly->GetPoints() || caller == this->GetParentTransformNode())
    {
    if (!this->IsUpdatingControlPointsFromPlane && !this->IsUpdatingPlaneFromControlPoints)
      {
      this->UpdatePlaneFromControlPoints();
      }
    }
  else if (((caller == this->ObjectToBaseMatrix.GetPointer()) || (caller == this->BaseToNodeMatrix.GetPointer()))
    && event == vtkCommand::ModifiedEvent)
    {
    if (!this->IsUpdatingControlPointsFromPlane && !this->IsUpdatingPlaneFromControlPoints)
      {
      this->UpdateInteractionHandleToWorldMatrix();
      this->UpdateControlPointsFromPlane();
      }
    }
  else if (caller == this->InteractionHandleToWorldMatrix && !this->IsUpdatingInteractionHandleToWorldMatrix)
    {
    if (!this->IsUpdatingControlPointsFromPlane && !this->IsUpdatingPlaneFromControlPoints)
      {
      this->BaseToNodeMatrix->DeepCopy(this->InteractionHandleToWorldMatrix);
      }
    }
  Superclass::ProcessMRMLEvents(caller, event, callData);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetSize(double planeSize_Object[2])
{
  planeSize_Object[0] = this->Size[0];
  planeSize_Object[1] = this->Size[1];
}

//----------------------------------------------------------------------------
double* vtkMRMLMarkupsPlaneNode::GetSize()
{
  return this->Size;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetSize(double x, double y)
{
  if (this->Size[0] == x && this->Size[1] == y)
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(this);
  this->Size[0] = x;
  this->Size[1] = y;
  /*this->UpdateControlPointsFromPlane();*/
  this->Modified();
}

//----------------------------------------------------------------------------
double* vtkMRMLMarkupsPlaneNode::GetPlaneBounds()
{
  return this->PlaneBounds;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetPlaneBounds(double planeBounds[6])
{
  if (!planeBounds)
    {
    vtkErrorMacro("GetPlaneBounds: Invalid argument");
    return;
    }

  for (int i = 0; i < 6; ++i)
    {
    planeBounds[i] = this->PlaneBounds[i];
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetPlaneBounds(double planeBounds[6])
{
  if (!planeBounds)
    {
    vtkErrorMacro("SetPlaneBounds: Invalid argument");
    return;
    }

  for (int i = 0; i < 6; ++i)
    {
    this->PlaneBounds[i] = planeBounds[i];
    }

  for (int i = 0; i < 2; ++i)
    {
    this->Size[i] = this->PlaneBounds[2*i+1] - this->PlaneBounds[2*i];
    }
  this->Modified();
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::SetPlaneBounds(double minX, double maxX, double minY, double maxY, double minZ, double maxZ)
{
  double bounds[6] = { minX, maxX, minY, maxY, minZ, maxZ };
  this->SetPlaneBounds(bounds);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::CreatePlane()
{
  if (this->GetNumberOfControlPoints() < 3)
    {
    this->AddNControlPoints(3 - this->GetNumberOfControlPoints());
    }

  double point0_Node[3] = { 0.0 };
  double point1_Node[3] = { 0.0 };
  double point2_Node[3] = { 0.0 };
  this->GetNthControlPointPosition(0, point0_Node);
  this->GetNthControlPointPosition(1, point1_Node);
  this->GetNthControlPointPosition(2, point2_Node);

  // Check if existing vectors are unique.
  double vectorPoint0ToPoint1_Node[3] = { 0.0 };
  double vectorPoint0ToPoint2_Node[3] = { 0.0 };
  vtkMath::Subtract(point1_Node, point0_Node, vectorPoint0ToPoint1_Node);
  vtkMath::Subtract(point2_Node, point0_Node, vectorPoint0ToPoint2_Node);

  bool pointChanged = false;
  double epsilon = 1e-5;
  if (vtkMath::Norm(vectorPoint0ToPoint1_Node) <= epsilon)
    {
    // Point1 is at same position as point0.
    // Move point1 away in x axis.
    double xVector[3] = { 1,0,0 };
    vtkMath::Add(point1_Node, xVector, point1_Node);
    pointChanged = true;
    }

  if (vtkMath::Norm(vectorPoint0ToPoint2_Node) <= epsilon)
    {
    // Point2 is at same position as point0.
    // Move point2 away in y axis.
    double yVector[3] = { 0,1,0 };
    vtkMath::Add(point2_Node, yVector, point2_Node);
    pointChanged = true;
    }

  vtkMath::Subtract(point1_Node, point0_Node, vectorPoint0ToPoint1_Node);
  vtkMath::Subtract(point2_Node, point0_Node, vectorPoint0ToPoint2_Node);
  if (vtkMath::Dot(vectorPoint0ToPoint1_Node, vectorPoint0ToPoint2_Node) >= 1.0 - epsilon)
    {
    // Point1 and point2 are along the same vector from point0.
    // Find a perpendicular vector and move point2.
    double perpendicular_Node[3] = { 0.0 };
    vtkMath::Perpendiculars(vectorPoint0ToPoint2_Node, perpendicular_Node, nullptr, 0.0);
    vtkMath::Add(point0_Node, perpendicular_Node, point2_Node);
    }

  if (pointChanged)
    {
    this->SetNthControlPointPosition(1, point1_Node[0], point1_Node[1], point1_Node[2]);
    this->SetNthControlPointPosition(2, point2_Node[0], point2_Node[1], point2_Node[2]);
    }
}

//----------------------------------------------------------------------------
vtkMatrix4x4* vtkMRMLMarkupsPlaneNode::GetObjectToBaseMatrix()
{
  return this->ObjectToBaseMatrix;
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::OnTransformNodeReferenceChanged(vtkMRMLTransformNode* transformNode)
{
  Superclass::OnTransformNodeReferenceChanged(transformNode);
  this->UpdateInteractionHandleToWorldMatrix();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdateInteractionHandleToWorldMatrix()
{
  this->IsUpdatingInteractionHandleToWorldMatrix = true;

  double handleX_World[3] = { 0.0, 0.0, 0.0 };
  double handleY_World[3] = { 0.0, 0.0, 0.0 };
  double handleZ_World[3] = { 0.0, 0.0, 0.0 };
  this->GetAxesWorld(handleX_World, handleY_World, handleZ_World);

  double origin_World[3] = { 0.0, 0.0, 0.0 };
  this->GetOriginWorld(origin_World);

  vtkNew<vtkMatrix4x4> handleToWorldMatrix;
  for (int i = 0; i < 3; ++i)
    {
    handleToWorldMatrix->SetElement(i, 0, handleX_World[i]);
    handleToWorldMatrix->SetElement(i, 1, handleY_World[i]);
    handleToWorldMatrix->SetElement(i, 2, handleZ_World[i]);
    handleToWorldMatrix->SetElement(i, 3, origin_World[i]);
    }
  this->InteractionHandleToWorldMatrix->DeepCopy(handleToWorldMatrix);
  this->IsUpdatingInteractionHandleToWorldMatrix = false;
}

//---------------------------------------------------------------------------
double vtkMRMLMarkupsPlaneNode::GetClosestPointOnPlaneWorld(const double posWorld[3], double closestPosWorld[3], bool infinitePlane/*=true*/)
{
  if (!posWorld)
    {
    vtkErrorMacro("GetClosestPointOnPlaneWorld: Invalid posWorld");
    return 0.0;
    }
  if (!closestPosWorld)
    {
    vtkErrorMacro("GetClosestPointOnPlaneWorld: Invalid closestPosWorld");
    return 0.0;
    }

  vtkNew<vtkMatrix4x4> objectToWorldMatrix;
  this->GetObjectToWorldMatrix(objectToWorldMatrix);

  vtkNew<vtkMatrix4x4> worldToObjectMatrix;
  worldToObjectMatrix->DeepCopy(objectToWorldMatrix);
  worldToObjectMatrix->Invert();

  double posWorld4[4] = { posWorld[0], posWorld[1], posWorld[2], 1.0 };
  double posPlane4[4] = { 0.0, 0.0, 0.0, 0.0 };
  worldToObjectMatrix->MultiplyPoint(posWorld4, posPlane4);

  double closestPosPlane4[4] = { posPlane4[0], posPlane4[1], posPlane4[2], posPlane4[3] };
  closestPosPlane4[2] = 0.0; // Project to plane

  if (!infinitePlane)
    {
    double planeSize_Object[2] = { 0.0, 0.0 };
    this->GetSize(planeSize_Object);
    closestPosPlane4[0] = std::max(std::min(closestPosPlane4[0], planeSize_Object[0] * 0.5), -1.0 * planeSize_Object[0] * 0.5);
    closestPosPlane4[1] = std::max(std::min(closestPosPlane4[1], planeSize_Object[1] * 0.5), -1.0 * planeSize_Object[1] * 0.5);
    }

  double closestPosWorld4[4] = { 0.0, 0.0, 0.0, 0.0 };
  objectToWorldMatrix->MultiplyPoint(closestPosPlane4, closestPosWorld4);
  for (int i = 0; i < 3; ++i)
    {
    closestPosWorld[i] = closestPosWorld4[i];
    }

  return std::sqrt(vtkMath::Distance2BetweenPoints(closestPosWorld, posWorld));
}

//---------------------------------------------------------------------------
vtkMRMLStorageNode* vtkMRMLMarkupsPlaneNode::CreateDefaultStorageNode()
{
  vtkMRMLScene* scene = this->GetScene();
  if (scene == nullptr)
    {
    vtkErrorMacro("CreateDefaultStorageNode failed: scene is invalid");
    return nullptr;
    }
  return vtkMRMLStorageNode::SafeDownCast(
    scene->CreateNodeByClass("vtkMRMLMarkupsPlaneJsonStorageNode"));
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::CreateDefaultDisplayNodes()
{
  if (this->GetDisplayNode() != nullptr &&
    vtkMRMLMarkupsPlaneDisplayNode::SafeDownCast(this->GetDisplayNode()) != nullptr)
    {
    // display node already exists
    return;
    }
  if (this->GetScene() == nullptr)
    {
    vtkErrorMacro("vtkMRMLMarkupsPlaneNode::CreateDefaultDisplayNodes failed: scene is invalid");
    return;
    }
  vtkMRMLMarkupsPlaneDisplayNode* dispNode = vtkMRMLMarkupsPlaneDisplayNode::SafeDownCast(
    this->GetScene()->AddNewNodeByClass("vtkMRMLMarkupsPlaneDisplayNode"));
  if (!dispNode)
    {
    vtkErrorMacro("vtkMRMLMarkupsPlaneNode::CreateDefaultDisplayNodes failed: scene failed to instantiate a vtkMRMLMarkupsPlaneDisplayNode node");
    return;
    }
  this->SetAndObserveDisplayNodeID(dispNode->GetID());
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdatePlaneFromControlPoints()
{
  if (this->IsUpdatingControlPointsFromPlane || this->IsUpdatingPlaneFromControlPoints)
    {
    return;
    }

  this->IsUpdatingPlaneFromControlPoints = true;
    {
    // Block events in this scope
    MRMLNodeModifyBlocker blocker(this);

    switch (this->PlaneType)
      {
      case PlaneTypePointNormal:
        this->UpdatePlaneFromPointNormal();
        break;
      case PlaneType3Points:
        this->UpdatePlaneFrom3Points();
        break;
      case PlaneTypePlaneFit:
        this->UpdatePlaneFromPlaneFit();
        break;
      default:
        break;
      }
    }
  this->IsUpdatingPlaneFromControlPoints = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdatePlaneFromPointNormal()
{
  double origin_Node[3] = { 0.0, 0.0, 0.0 };
  if (this->GetNumberOfControlPoints() > 0)
    {
    this->GetNthControlPointPosition(0, origin_Node);
    }

  double newX_Node[3] = { 0.0, 0.0, 0.0 };
  double newY_Node[3] = { 0.0, 0.0, 0.0 };
  double newZ_Node[3] = { 0.0, 0.0, 0.0 };
  if (this->GetNumberOfControlPoints() > 1)
    {
    double normalPoint_Node[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPosition(1, normalPoint_Node);
    vtkMath::Subtract(normalPoint_Node, origin_Node, newZ_Node);
    vtkMath::Normalize(newZ_Node);

    vtkMath::Perpendiculars(newZ_Node, newX_Node, newY_Node, 0.0);
    }
  else
    {
    this->GetAxes(newX_Node, newY_Node, newZ_Node);
    }

  double oldZ_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetNormal(oldZ_Node);

  vtkNew<vtkTransform> baseToNodeTransform;
  baseToNodeTransform->PostMultiply();
  double oldOrigin_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(oldOrigin_Node);
  baseToNodeTransform->Concatenate(this->BaseToNodeMatrix);
  baseToNodeTransform->Translate(-oldOrigin_Node[0], -oldOrigin_Node[1], -oldOrigin_Node[2]);

  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(oldZ_Node, newZ_Node));
  double epsilon = 0.001;
  if (angle > epsilon)
    {
    double rotationVector_World[3] = { 1.0, 0.0, 0.0 };
    vtkMath::Cross(oldZ_Node, newZ_Node, rotationVector_World);
    vtkMath::Normalize(rotationVector_World);
    baseToNodeTransform->RotateWXYZ(angle, rotationVector_World);
    }
  baseToNodeTransform->Translate(origin_Node);
  this->BaseToNodeMatrix->DeepCopy(baseToNodeTransform->GetMatrix());

  //if (this->GetNumberOfDefinedControlPoints() >= 1)
  //  {
  //  this->SetIsPlaneValid(true);
  //  }
  //else
  //  {
  //  this->SetIsPlaneValid(false);
  //  }
  this->SetIsPlaneValid(true);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdatePlaneFrom3Points()
{
  if (this->GetNumberOfControlPoints() < 3)
    {
    // Not enough points to define the plane
    this->SetIsPlaneValid(false);
    this->BaseToNodeMatrix->Identity();
    if (this->SizeMode == vtkMRMLMarkupsPlaneNode::SizeModeAuto)
      {
      this->SetSize(0.0, 0.0);
      }
    return;
    }

  double point0_Node[3] = { 0.0 };
  this->GetNthControlPointPosition(0, point0_Node);

  vtkNew<vtkMatrix4x4> baseToNodeMatrix;
  for (int i = 0; i < 3; ++i)
    {
    baseToNodeMatrix->SetElement(i, 3, point0_Node[i]);
    }

  if (this->GetNumberOfControlPoints() >= 3)
    {
    double point1_Node[3] = { 0.0 };
    double point2_Node[3] = { 0.0 };

    this->GetNthControlPointPosition(1, point1_Node);
    this->GetNthControlPointPosition(2, point2_Node);

    double xAxis_Node[3] = { 0.0 };
    double yAxis_Node[3] = { 0.0 };
    double zAxis_Node[3] = { 0.0 };
    this->CalculateAxesFromPoints(point0_Node, point1_Node, point2_Node, xAxis_Node, yAxis_Node, zAxis_Node);
    for (int i = 0; i < 3; ++i)
      {
      baseToNodeMatrix->SetElement(i, 0, xAxis_Node[i]);
      baseToNodeMatrix->SetElement(i, 1, yAxis_Node[i]);
      baseToNodeMatrix->SetElement(i, 2, zAxis_Node[i]);
      }
    }
  this->BaseToNodeMatrix->DeepCopy(baseToNodeMatrix);

  // Size mode auto means we need to recalculate the diameter of the plane from the control points.
  if (this->SizeMode == vtkMRMLMarkupsPlaneNode::SizeModeAuto)
    {
    double point0_Node[3] = { 0.0, 0.0, 0.0 };
    double point1_Node[3] = { 0.0, 0.0, 0.0 };
    double point2_Node[3] = { 0.0, 0.0, 0.0 };
    this->GetNthControlPointPosition(0, point0_Node);
    this->GetNthControlPointPosition(1, point1_Node);
    this->GetNthControlPointPosition(2, point2_Node);

    vtkNew<vtkMatrix4x4> baseToNodeMatrix;
     this->GetBaseToNodeMatrix(baseToNodeMatrix);

    vtkNew<vtkTransform> nodeToBaseTransform;
    nodeToBaseTransform->SetMatrix(baseToNodeMatrix);
    nodeToBaseTransform->Inverse();

    double point0_Base[3] = { 0.0, 0.0, 0.0 };
    double point1_Base[3] = { 0.0, 0.0, 0.0 };
    double point2_Base[3] = { 0.0, 0.0, 0.0 };
    nodeToBaseTransform->TransformPoint(point0_Node, point0_Base);
    nodeToBaseTransform->TransformPoint(point1_Node, point1_Base);
    nodeToBaseTransform->TransformPoint(point2_Node, point2_Base);

    double xMax = std::max({ std::abs(point0_Base[0]), std::abs(point1_Base[0]), std::abs(point2_Base[0]) });
    double yMax = std::max({ std::abs(point0_Base[1]), std::abs(point1_Base[1]), std::abs(point2_Base[1]) });

    this->SetSize(2 * xMax * this->AutoSizeScalingFactor, 2 * yMax * this->AutoSizeScalingFactor);
    }

  this->SetIsPlaneValid(true);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GetClosestFitPlaneFromControlPoints(vtkMatrix4x4* closestFitPlane)
{
  // The orientation of the coordinate system is adjusted so that the z axis aligns with the normal of the
  // best fit plane defined by the control points.

  int numberOfControlPoints = this->GetNumberOfMarkups();
  vtkNew<vtkPoints> controlPoints_World;
  for (int i = 0; i < numberOfControlPoints; ++i)
    {
    double controlPointPosition_World[3] = { 0.0 };
    this->GetNthControlPointPositionWorld(i, controlPointPosition_World);
    controlPoints_World->InsertNextPoint(controlPointPosition_World);
    }

  vtkNew<vtkPlane> bestFitPlane_Node;
  vtkAddonMathUtilities::FitPlaneToPoints(controlPoints_World, bestFitPlane_Node);

  double newZ_Node[3] = { 0.0, 0.0, 0.0 };
  bestFitPlane_Node->GetNormal(newZ_Node);

  double oldZ_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetNormal(oldZ_Node);

  if (vtkMath::Dot(oldZ_Node, newZ_Node) < -0.9)
    {
    vtkMath::MultiplyScalar(newZ_Node, -1.0);
    }

  vtkNew<vtkTransform> baseToNodeTransform;
  baseToNodeTransform->PostMultiply();
  double oldOrigin_Node[3] = { 0.0, 0.0, 0.0 };
  this->GetOrigin(oldOrigin_Node);
  baseToNodeTransform->Concatenate(this->BaseToNodeMatrix);
  baseToNodeTransform->Translate(-oldOrigin_Node[0], -oldOrigin_Node[1], -oldOrigin_Node[2]);

  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(oldZ_Node, newZ_Node));
  double epsilon = 0.001;
  if (angle > epsilon)
    {
    double rotationVector_World[3] = { 1.0, 0.0, 0.0 };
    vtkMath::Cross(oldZ_Node, newZ_Node, rotationVector_World);
    vtkMath::Normalize(rotationVector_World);
    baseToNodeTransform->RotateWXYZ(angle, rotationVector_World);
    }
  baseToNodeTransform->Translate(bestFitPlane_Node->GetOrigin());
  closestFitPlane->DeepCopy(baseToNodeTransform->GetMatrix());
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdatePlaneFromPlaneFit()
{
  // The origin of the coordinate system is at the center of mass of the control points
  if (this->GetNumberOfControlPoints() < 3)
    {
    this->SetIsPlaneValid(false);
    this->BaseToNodeMatrix->Identity();
    if (this->SizeMode == vtkMRMLMarkupsPlaneNode::SizeModeAuto)
      {
      this->SetSize(0.0, 0.0);
      }
    this->SetIsPlaneValid(false);
    return;
    }

  // The orientation of the coordinate system is adjusted so that the z axis aligns with the normal of the
  // best fit plane defined by the control points.

  vtkNew<vtkMatrix4x4> bestFitMatrix_Node;
  this->GetClosestFitPlaneFromControlPoints(bestFitMatrix_Node);

  // Update the base to node matrix
  this->BaseToNodeMatrix->DeepCopy(bestFitMatrix_Node);

  // Size mode auto means we need to recalculate the diameter of the plane from the control points.
  if (this->SizeMode == vtkMRMLMarkupsPlaneNode::SizeModeAuto)
    {
    double xMax = VTK_DOUBLE_MIN;
    double yMax = VTK_DOUBLE_MIN;
    for (int i = 0; i < this->GetNumberOfControlPoints(); ++i)
      {
      int status = this->GetNthControlPointPositionStatus(i);
      if (status == vtkMRMLMarkupsNode::PositionUndefined ||
        status == vtkMRMLMarkupsNode::PositionMissing)
        {
        continue;
        }

      double point_Node[3] = { 0.0, 0.0, 0.0 };
      this->GetNthControlPointPosition(i, point_Node);

      vtkNew<vtkTransform> nodeToBaseTransform;
      nodeToBaseTransform->SetMatrix(this->BaseToNodeMatrix);
      nodeToBaseTransform->Inverse();

      double point_Base[3] = { 0.0, 0.0, 0.0 };
      nodeToBaseTransform->TransformPoint(point_Node, point_Base);

      xMax = std::max(xMax, std::abs(point_Base[0]));
      yMax = std::max(yMax, std::abs(point_Base[1]));
      }

    this->SetSize(2 * xMax * this->AutoSizeScalingFactor, 2 * yMax * this->AutoSizeScalingFactor);
    }

  this->SetIsPlaneValid(true);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdateControlPointsFromPlane()
{
  if (this->IsUpdatingControlPointsFromPlane || this->IsUpdatingPlaneFromControlPoints || !this->IsPlaneValid)
    {
    return;
    }

  this->IsUpdatingControlPointsFromPlane = true;

  {
    // Block events in this scope
    MRMLNodeModifyBlocker blocker(this);

    switch (this->PlaneType)
      {
      case vtkMRMLMarkupsPlaneNode::PlaneTypePointNormal:
        this->UpdateControlPointsFromPointNormal();
        break;
      case vtkMRMLMarkupsPlaneNode::PlaneType3Points:
        this->UpdateControlPointsFrom3Points();
        break;
      case vtkMRMLMarkupsPlaneNode::PlaneTypePlaneFit:
        this->UpdateControlPointsFromPlaneFit();
        break;
      default:
        break;
      }
  }

  this->IsUpdatingControlPointsFromPlane = false;
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdateControlPointsFromPointNormal()
{
  if (this->GetNumberOfDefinedControlPoints() > 1)
    {
    while (this->GetNumberOfDefinedControlPoints() > 1)
      {
      this->RemoveNthControlPoint(1);
      }
    this->RequiredNumberOfControlPoints = 2;
    this->MaximumNumberOfControlPoints = 2;
    }
  else
    {
    this->RequiredNumberOfControlPoints = 1;
    this->MaximumNumberOfControlPoints = 2;
    }

  if (this->IsPlaneValid && this->GetNumberOfControlPoints() == 0)
    {
    this->AddControlPoint(vtkVector3d());
    }

  if (this->IsPlaneValid && this->GetNumberOfControlPoints() > 0)
    {
    double origin[3] = { 0,0,0 };
    this->GetOrigin(origin);
    this->SetNthControlPointPosition(0, origin[0], origin[1], origin[2]);
    }
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdateControlPointsFrom3Points()
{

}
#include <vtkTransformPolyDataFilter.h>
//----------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::UpdateControlPointsFromPlaneFit()
{
  vtkNew<vtkMatrix4x4> bestFitMatrix_Node;
  this->GetClosestFitPlaneFromControlPoints(bestFitMatrix_Node);

  double oldZ_Node[4] = { 0.0, 0.0, 1.0, 0.0 };
  bestFitMatrix_Node->MultiplyPoint(oldZ_Node, oldZ_Node);

  double newZ_Node[4] = { 0.0, 0.0, 1.0, 0.0 };
  this->BaseToNodeMatrix->MultiplyPoint(newZ_Node, newZ_Node);

  vtkNew<vtkMatrix4x4> oldNodeToBase;
  vtkMatrix4x4::Invert(bestFitMatrix_Node, oldNodeToBase);

  double scale[3] = { 1.0, 1.0, 1.0 };

  // Size mode auto means we need to recalculate the diameter of the plane from the control points.
  if (this->SizeMode == vtkMRMLMarkupsPlaneNode::SizeModeAuto)
    {
    double xMax = VTK_DOUBLE_MIN;
    double yMax = VTK_DOUBLE_MIN;
    for (int i = 0; i < this->GetNumberOfControlPoints(); ++i)
      {
      double point_Node[3] = { 0.0, 0.0, 0.0 };
      this->GetNthControlPointPosition(i, point_Node);

      vtkNew<vtkTransform> nodeToBaseTransform;
      nodeToBaseTransform->SetMatrix(oldNodeToBase);

      double point_Base[3] = { 0.0, 0.0, 0.0 };
      nodeToBaseTransform->TransformPoint(point_Node, point_Base);

      xMax = std::max(xMax, std::abs(point_Base[0]));
      yMax = std::max(yMax, std::abs(point_Base[1]));
      }

    double size[2] = { 2.0 * xMax * this->AutoSizeScalingFactor, 2.0 * yMax * this->AutoSizeScalingFactor };
    for (int i = 0; i < 2; ++i)
      {
      if (size[i] == 0.0)
        {
        continue;
        }
      scale[i] = this->Size[i] / size[i];
      }
    }

  vtkNew<vtkTransform> oldToNewTransform;
  oldToNewTransform->PostMultiply();
  oldToNewTransform->Concatenate(oldNodeToBase);
  oldToNewTransform->Scale(scale);
  oldToNewTransform->Concatenate(this->BaseToNodeMatrix);

  vtkNew<vtkTransformPolyDataFilter> transformPoints;
  transformPoints->SetInputData(this->CurveInputPoly);
  transformPoints->SetTransform(oldToNewTransform);
  transformPoints->Update();
  this->SetControlPointPositionsWorld(transformPoints->GetOutput()->GetPoints());
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GenerateOrthogonalMatrix(vtkMatrix4x4* inputMatrix,
  vtkMatrix4x4* outputMatrix, vtkAbstractTransform* transform/*=nullptr*/, bool applyScaling/*=true*/)
{

  double xAxis[3] = { 0.0, 0.0, 0.0 };
  double yAxis[3] = { 0.0, 0.0, 0.0 };
  double zAxis[3] = { 0.0, 0.0, 0.0 };
  double origin[3] = { 0.0,0.0, 0.0 };
  for (int i = 0; i < 3; ++i)
    {
    xAxis[i] = inputMatrix->GetElement(i, 0);
    yAxis[i] = inputMatrix->GetElement(i, 1);
    zAxis[i] = inputMatrix->GetElement(i, 2);
    origin[i] = inputMatrix->GetElement(i, 3);
    }
  vtkMRMLMarkupsPlaneNode::GenerateOrthogonalMatrix(xAxis, yAxis, zAxis, origin, outputMatrix, transform, applyScaling);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsPlaneNode::GenerateOrthogonalMatrix(double xAxis[3], double yAxis[3], double zAxis[3], double origin[3],
  vtkMatrix4x4* outputMatrix, vtkAbstractTransform* transform/*=nullptr*/, bool applyScaling/*=true*/)
{
  if (!xAxis || !yAxis || !zAxis || !origin || !transform || !outputMatrix)
    {
    vtkErrorWithObjectMacro(nullptr, "GenerateOrthogonalMatrix: Invalid arguments");
    return;
    }

  double xAxisTransformed[3] = { xAxis[0],  xAxis[1], xAxis[2] };
  double yAxisTransformed[3] = { yAxis[0],  yAxis[1], yAxis[2] };
  double zAxisTransformed[3] = { zAxis[0],  zAxis[1], zAxis[2] };
  double originTransformed[3] = { origin[0],  origin[1], origin[2] };

  double xAxisScale = vtkMath::Norm(xAxis);
  double yAxisScale = vtkMath::Norm(yAxis);
  double zAxisScale = vtkMath::Norm(zAxis);

  if (transform)
    {
    transform->TransformVectorAtPoint(origin, xAxis, xAxisTransformed);
    transform->TransformVectorAtPoint(origin, yAxis, yAxisTransformed);
    transform->TransformVectorAtPoint(origin, zAxis, zAxisTransformed);
    transform->TransformPoint(origin, originTransformed);
    }

  vtkMath::Cross(xAxisTransformed, yAxisTransformed, zAxisTransformed);
  vtkMath::Normalize(zAxisTransformed);
  vtkMath::Cross(zAxisTransformed, xAxisTransformed, yAxisTransformed);
  vtkMath::Normalize(yAxisTransformed);
  vtkMath::Cross(yAxisTransformed, zAxisTransformed, xAxisTransformed);
  vtkMath::Normalize(xAxisTransformed);

  if (applyScaling)
    {
    if (transform)
      {
      vtkAbstractTransform* inverseTransform = transform->GetInverse();
      xAxisScale /= vtkMath::Norm(inverseTransform->TransformVectorAtPoint(originTransformed, xAxisTransformed));
      yAxisScale /= vtkMath::Norm(inverseTransform->TransformVectorAtPoint(originTransformed, yAxisTransformed));
      zAxisScale /= vtkMath::Norm(inverseTransform->TransformVectorAtPoint(originTransformed, zAxisTransformed));
      }
    vtkMath::MultiplyScalar(xAxisTransformed, xAxisScale);
    vtkMath::MultiplyScalar(yAxisTransformed, yAxisScale);
    vtkMath::MultiplyScalar(zAxisTransformed, zAxisScale);
    }

  for (int i = 0; i < 3; ++i)
    {
    outputMatrix->SetElement(i, 0, xAxisTransformed[i]);
    outputMatrix->SetElement(i, 1, yAxisTransformed[i]);
    outputMatrix->SetElement(i, 2, zAxisTransformed[i]);
    outputMatrix->SetElement(i, 3, originTransformed[i]);
    }
}
