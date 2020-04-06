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

  double xVector[4] = { 1, 0, 0, 0 };
  this->InteractionHandleToWorld->MultiplyPoint(xVector, xVector);

  double lineVector[4] = { 0 };
  double linePoint0[3];
  this->GetNthControlPointPosition(0, linePoint0);
  double linePoint1[3];
  this->GetNthControlPointPosition(1, linePoint1);
  vtkMath::Subtract(linePoint1, linePoint0, lineVector);

  double rotationVector[3] = { 0 };
  double angle = vtkMath::DegreesFromRadians(vtkMath::AngleBetweenVectors(lineVector, xVector));
  double epsilon = 0.001;
  if (angle < epsilon)
    {
    return;
    }
  vtkMath::Cross(xVector, lineVector, rotationVector);

  double origin[4] = { 0, 0, 0, 1 };
  this->InteractionHandleToWorld->MultiplyPoint(origin, origin);

  vtkNew<vtkTransform> modelToWorldMatrix;
  modelToWorldMatrix->PostMultiply();
  modelToWorldMatrix->Concatenate(this->InteractionHandleToWorld);
  modelToWorldMatrix->Translate(-origin[0], -origin[1], -origin[2]);
  modelToWorldMatrix->RotateWXYZ(angle, rotationVector);
  modelToWorldMatrix->Translate(origin);
  this->InteractionHandleToWorld->DeepCopy(modelToWorldMatrix->GetMatrix());
}
