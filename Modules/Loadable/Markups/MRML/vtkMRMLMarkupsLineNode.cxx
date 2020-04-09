/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// MRML includes
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsLineNode.h"
#include "vtkMRMLMarkupsFiducialStorageNode.h"
#include "vtkMatrix4x4.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLUnitNode.h"
#include "vtkTransform.h"

// VTK includes
#include <vtkNew.h>
#include <vtkObjectFactory.h>

// STD includes
#include <sstream>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsLineNode);


//----------------------------------------------------------------------------
vtkMRMLMarkupsLineNode::vtkMRMLMarkupsLineNode()
{
  this->MaximumNumberOfControlPoints = 2;
  this->RequiredNumberOfControlPoints = 2;
}

//----------------------------------------------------------------------------
vtkMRMLMarkupsLineNode::~vtkMRMLMarkupsLineNode()
= default;

//----------------------------------------------------------------------------
void vtkMRMLMarkupsLineNode::WriteXML(ostream& of, int nIndent)
{
  Superclass::WriteXML(of,nIndent);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsLineNode::ReadXMLAttributes(const char** atts)
{
  Superclass::ReadXMLAttributes(atts);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsLineNode::Copy(vtkMRMLNode *anode)
{
  Superclass::Copy(anode);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsLineNode::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os,indent);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsLineNode::UpdateMeasurements()
{
  this->RemoveAllMeasurements();
  if (this->GetNumberOfDefinedControlPoints() == 2)
    {
    double p1[3] = { 0.0 };
    double p2[3] = { 0.0 };
    this->GetNthControlPointPositionWorld(0, p1);
    this->GetNthControlPointPositionWorld(1, p2);
    double length = sqrt(vtkMath::Distance2BetweenPoints(p1, p2));
    std::string printFormat;
    std::string unit = "mm";
    vtkMRMLUnitNode* unitNode = GetUnitNode("length");
    if (unitNode)
      {
      if (unitNode->GetSuffix())
        {
        unit = unitNode->GetSuffix();
        }
      length = unitNode->GetDisplayValueFromValue(length);
      printFormat = unitNode->GetDisplayStringFormat();
      }
    this->SetNthMeasurement(0, "length", length, unit, printFormat);
    }
  this->WriteMeasurementsToDescription();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsLineNode::UpdateInteractionHandleToWorld()
{
  Superclass::UpdateInteractionHandleToWorld();
  if (this->GetNumberOfControlPoints() < 2)
    {
    return;
    }

  double modelX_Local[4] = { 1.0, 0.0, 0.0, 0.0 };
  this->InteractionHandleModelToLocal->MultiplyPoint(modelX_Local, modelX_Local);

  double point0_Local[3];
  this->GetNthControlPointPosition(0, point0_Local);
  double point1_Local[3];
  this->GetNthControlPointPosition(1, point1_Local);

  double vectorPoint0ToPoint1_Local[4] = { 0.0 };
  vtkMath::Subtract(point1_Local, point0_Local, vectorPoint0ToPoint1_Local);

  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(vectorPoint0ToPoint1_Local, modelX_Local));
  double epsilon = 1e-5;
  if (angle < epsilon)
    {
    return;
    }

  double rotationVector_Local[3] = { 0.0 };
  vtkMath::Cross(modelX_Local, vectorPoint0ToPoint1_Local, rotationVector_Local);

  double origin_Local[4] = { 0.0, 0.0, 0.0, 1.0 };
  this->InteractionHandleModelToLocal->MultiplyPoint(origin_Local, origin_Local);

  vtkNew<vtkTransform> modelToWorldMatrix;
  modelToWorldMatrix->PostMultiply();
  modelToWorldMatrix->Concatenate(this->InteractionHandleModelToLocal);
  modelToWorldMatrix->Translate(-origin_Local[0], -origin_Local[1], -origin_Local[2]);
  modelToWorldMatrix->RotateWXYZ(angle, rotationVector_Local);
  modelToWorldMatrix->Translate(origin_Local);
  this->InteractionHandleModelToLocal->DeepCopy(modelToWorldMatrix->GetMatrix());
}
