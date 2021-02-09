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

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

#include <vtkCodedEntry.h>
#include "vtkMRMLMarkupsROIJsonStorageNode.h"
#include "vtkMRMLMarkupsDisplayNode.h"
#include "vtkMRMLMarkupsROINode.h"

#include "vtkMRMLScene.h"
#include "vtkSlicerVersionConfigure.h"

#include "vtkDoubleArray.h"
#include "vtkObjectFactory.h"
#include "vtkStringArray.h"
#include <vtksys/SystemTools.hxx>

#include <vtkMRMLMarkupsROIJsonStorageNode_Private.h>

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::vtkInternalROI::vtkInternalROI(vtkMRMLMarkupsROIJsonStorageNode* external)
  : vtkMRMLMarkupsJsonStorageNode::vtkInternal(external)
{
}

//---------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::vtkInternalROI::~vtkInternalROI()
{
}

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::vtkInternalROI::WriteMarkup(
  rapidjson::PrettyWriter<rapidjson::FileWriteStream>& writer, vtkMRMLMarkupsNode* markupsNode)
{
  bool success = vtkMRMLMarkupsJsonStorageNode::vtkInternal::WriteMarkup(writer, markupsNode);
  success && this->WriteROIProperties(writer, vtkMRMLMarkupsROINode::SafeDownCast(markupsNode));
  return success;
}

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::vtkInternalROI::WriteROIProperties(
  rapidjson::PrettyWriter<rapidjson::FileWriteStream>& writer, vtkMRMLMarkupsROINode* roiNode)
{
  if (!roiNode)
    {
    return false;
    }

  writer.Key("roiType");
  writer.String(roiNode->GetROITypeAsString(roiNode->GetROIType()));

  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetOrigin(origin_Local);
  writer.Key("origin");
  this->WriteVector(writer, origin_Local);

  double sideLengths[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetSideLengths(sideLengths);
  writer.Key("sideLengths");
  this->WriteVector(writer, sideLengths);

  double xAxis_Local[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetAxisLocal(0, xAxis_Local);
  writer.Key("xAxis");
  this->WriteVector(writer, xAxis_Local);

  double yAxis_Local[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetAxisLocal(1, yAxis_Local);
  writer.Key("yAxis");
  this->WriteVector(writer, yAxis_Local);

  double zAxis_Local[3] = { 0.0, 0.0, 0.0 };
  roiNode->GetAxisLocal(2, zAxis_Local);
  writer.Key("zAxis");
  this->WriteVector(writer, zAxis_Local);

  return true;
}

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::vtkInternalROI::UpdateMarkupsNodeFromJsonValue(vtkMRMLMarkupsNode* markupsNode, rapidjson::Value& markupObject)
{
  if (!markupsNode)
    {
    vtkErrorWithObjectMacro(this->External, "vtkMRMLMarkupsJsonStorageNode::vtkInternalROI::UpdateMarkupsNodeFromJsonDocument failed: invalid markupsNode");
    return false;
    }

  MRMLNodeModifyBlocker blocker(markupsNode);

  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(markupsNode);

  bool success = true;

  if (markupObject.HasMember("roiType"))
  {
    rapidjson::Value& roiTypeItem = markupObject["roiType"];
    std::string roiType = roiTypeItem.GetString();
    roiNode->SetROIType(roiNode->GetROITypeFromString(roiType.c_str()));
  }

  double origin_Local[3] = { 0.0, 0.0, 0.0 };
  if (markupObject.HasMember("origin"))
  {
    rapidjson::Value& originItem = markupObject["origin"];
    success &= this->ReadVector(originItem, origin_Local);
  }

  if (markupObject.HasMember("sideLengths"))
  {
    rapidjson::Value& sideLengthsItem = markupObject["sideLengths"];
    double sideLengths[3] = { 0.0, 0.0, 0.0 };
    success &= this->ReadVector(sideLengthsItem, sideLengths);
    roiNode->SetSideLengths(sideLengths);
  }

  double xAxis_Local[3] = { 1.0, 0.0, 0.0 };
  if (markupObject.HasMember("xAxis"))
  {
    rapidjson::Value& xAxisItem = markupObject["xAxis"];
    success &= this->ReadVector(xAxisItem, xAxis_Local);
  }

  double yAxis_Local[3] = { 0.0, 1.0, 0.0 };
  if (markupObject.HasMember("yAxis"))
  {
    rapidjson::Value& yAxisItem = markupObject["yAxis"];
    success &= this->ReadVector(yAxisItem, yAxis_Local);
  }

  double zAxis_Local[3] = { 0.0, 0.0, 1.0 };
  if (markupObject.HasMember("zAxis"))
  {
    rapidjson::Value& zAxisItem = markupObject["zAxis"];
    success &= this->ReadVector(zAxisItem, zAxis_Local);
  }

  vtkNew<vtkMatrix4x4> roiToLocalMatrix;
  for (int i = 0; i < 3; ++i)
  {
    roiToLocalMatrix->SetElement(i, 0, xAxis_Local[i]);
    roiToLocalMatrix->SetElement(i, 1, yAxis_Local[i]);
    roiToLocalMatrix->SetElement(i, 2, zAxis_Local[i]);
    roiToLocalMatrix->SetElement(i, 3, origin_Local[i]);
  }
  roiNode->GetROIToLocalMatrix()->DeepCopy(roiToLocalMatrix);

  return vtkInternal::UpdateMarkupsNodeFromJsonValue(markupsNode, markupObject);
}


//------------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROIJsonStorageNode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::vtkMRMLMarkupsROIJsonStorageNode()
{
  this->Internal = new vtkInternalROI(this);
}

//----------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::~vtkMRMLMarkupsROIJsonStorageNode() = default;

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::CanReadInReferenceNode(vtkMRMLNode *refNode)
{
  return refNode->IsA("vtkMRMLMarkupsROINode");
}
