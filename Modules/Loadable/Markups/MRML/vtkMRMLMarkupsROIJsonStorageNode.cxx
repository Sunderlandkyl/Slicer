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
#include "vtkMRMLMeasurementConstant.h"

#include "vtkMRMLScene.h"
#include "vtkSlicerVersionConfigure.h"

#include "vtkDoubleArray.h"
#include "vtkObjectFactory.h"
#include "vtkStringArray.h"
#include <vtksys/SystemTools.hxx>

#include "itkNumberToString.h"

// Relax JSON standard and allow reading/writing of nan and inf
// values. Such values should not normally occur, but if they do then
// it is easier to troubleshoot problems if numerical values are the
// same in memory and files.
// kWriteNanAndInfFlag = 2,        //!< Allow writing of Infinity, -Infinity and NaN.
#define RAPIDJSON_WRITE_DEFAULT_FLAGS 2
// kParseNanAndInfFlag = 256,      //!< Allow parsing NaN, Inf, Infinity, -Inf and -Infinity as doubles.
#define RAPIDJSON_PARSE_DEFAULT_FLAGS 256

#include "rapidjson/document.h"     // rapidjson's DOM-style API
#include "rapidjson/prettywriter.h" // for stringify JSON
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"

#include <vtkMRMLMarkupsJsonStorageNode_Private.h>

namespace
{
  const std::string MARKUPS_SCHEMA =
    "https://raw.githubusercontent.com/slicer/slicer/master/Modules/Loadable/Markups/Resources/Schema/markups-schema-v1.0.0.json#";
}

//---------------------------------------------------------------------------
class vtkMRMLMarkupsROIJsonStorageNode::vtkInternal2 : public vtkMRMLMarkupsJsonStorageNode::vtkInternal
{
public:
  vtkInternal2(vtkMRMLMarkupsROIJsonStorageNode* external);
  ~vtkInternal2();

  bool UpdateMarkupsNodeFromJsonValue(vtkMRMLMarkupsNode* markupsNode, rapidjson::Value& markupObject) override;
  bool WriteROIProperties(rapidjson::PrettyWriter<rapidjson::FileWriteStream>& writer, vtkMRMLMarkupsROINode* markupsNode);
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::vtkInternal2::vtkInternal2(vtkMRMLMarkupsROIJsonStorageNode* external)
  : vtkMRMLMarkupsJsonStorageNode::vtkInternal(external)
{
}

//---------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::vtkInternal2::~vtkInternal2()
{
}

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::vtkInternal2::WriteROIProperties(
  rapidjson::PrettyWriter<rapidjson::FileWriteStream>& writer, vtkMRMLMarkupsROINode* roiNode)
{
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

//------------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLMarkupsROIJsonStorageNode);

//----------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::vtkMRMLMarkupsROIJsonStorageNode()
{
  this->Internal = new vtkInternal2(this);
  this->DefaultWriteFileExtension = "mrk.json";
}

//----------------------------------------------------------------------------
vtkMRMLMarkupsROIJsonStorageNode::~vtkMRMLMarkupsROIJsonStorageNode() = default;

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROIJsonStorageNode::WriteXML(ostream& of, int nIndent)
{
  Superclass::WriteXML(of,nIndent);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROIJsonStorageNode::ReadXMLAttributes(const char** atts)
{
  Superclass::ReadXMLAttributes(atts);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROIJsonStorageNode::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os,indent);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROIJsonStorageNode::Copy(vtkMRMLNode *anode)
{
  Superclass::Copy(anode);
}

//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::CanReadInReferenceNode(vtkMRMLNode *refNode)
{
  return refNode->IsA("vtkMRMLMarkupsNode");
}


//----------------------------------------------------------------------------
vtkMRMLMarkupsNode* vtkMRMLMarkupsROIJsonStorageNode::AddNewMarkupsNodeFromFile(const char* filePath, const char* nodeName/*=nullptr*/, int markupIndex/*=0*/)
{
  vtkMRMLScene* scene = this->GetScene();
  if (!scene)
    {
    vtkErrorMacro("vtkMRMLMarkupsROIJsonStorageNode::AddMarkupsNodeFromFile failed: invalid scene");
    return nullptr;
    }
  if (!filePath)
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: invalid filename");
    return nullptr;
    }
  rapidjson::Document* jsonRoot = this->Internal->CreateJsonDocumentFromFile(filePath);
  if (!jsonRoot)
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: error opening the file '" << filePath);
    return nullptr;
    }

  rapidjson::Value& markups = (*jsonRoot)["markups"];
  if (!markups.IsArray())
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::AddNewMarkupsNodeFromFile failed: cannot read markup from file " << filePath
      << " (does not contain valid 'markups' array)");
    delete jsonRoot;
    return nullptr;
    }
  if (markupIndex >= static_cast<int>(markups.GetArray().Size()))
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::AddNewMarkupsNodeFromFile failed: cannot read markup from file " << filePath
      << " requested markup index " << markupIndex << " is not found (number of available markups: " << markups.GetArray().Size());
    delete jsonRoot;
    return nullptr;
    }

  rapidjson::Value& markup = markups.GetArray()[markupIndex];
  std::string className = this->Internal->GetMarkupsClassNameFromJsonValue(markup);
  if (!markup.HasMember("type"))
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::AddNewMarkupsNodeFromFile failed: cannot find required 'type' value");
    delete jsonRoot;
    return nullptr;
    }
  std::string markupsType = markup["type"].GetString();

  std::string newNodeName;
  if (nodeName && strlen(nodeName)>0)
    {
    newNodeName = nodeName;
    }
  else
    {
    newNodeName = scene->GetUniqueNameByString(this->GetFileNameWithoutExtension(filePath).c_str());
    }
  vtkMRMLMarkupsNode* markupsNode = vtkMRMLMarkupsNode::SafeDownCast(scene->AddNewNodeByClass(className.c_str(), newNodeName));
  if (!markupsNode)
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: cannot instantiate class '" << className);
    delete jsonRoot;
    return nullptr;
    }

  bool success = true;
  success = success && this->Internal->UpdateMarkupsNodeFromJsonValue(markupsNode, markup);

  vtkMRMLMarkupsDisplayNode* displayNode = nullptr;
  if (success && markupsNode)
    {
    markupsNode->CreateDefaultDisplayNodes();
    displayNode = vtkMRMLMarkupsDisplayNode::SafeDownCast(markupsNode->GetDisplayNode());
    if (displayNode && markup.HasMember("display"))
      {
      success = success && this->Internal->UpdateMarkupsDisplayNodeFromJsonValue(displayNode, markup);
      }
    }

  delete jsonRoot;

  if (!success)
    {
    if (displayNode)
      {
      scene->RemoveNode(displayNode);
      }
    if (markupsNode)
      {
      scene->RemoveNode(markupsNode);
      }
    return nullptr;
    }

  markupsNode->SetAndObserveStorageNodeID(this->GetID());
  return markupsNode;
}


//----------------------------------------------------------------------------
bool vtkMRMLMarkupsROIJsonStorageNode::vtkInternal2::UpdateMarkupsNodeFromJsonValue(vtkMRMLMarkupsNode* markupsNode, rapidjson::Value& markupObject)
{
  if (!markupsNode)
    {
    vtkErrorWithObjectMacro(this->External, "vtkMRMLMarkupsJsonStorageNode::vtkInternal2::UpdateMarkupsNodeFromJsonDocument failed: invalid markupsNode");
    return false;
    }

  MRMLNodeModifyBlocker blocker(markupsNode);

  vtkInternal::UpdateMarkupsNodeFromJsonValue(markupsNode, markupObject);

  vtkMRMLMarkupsROINode* roiNode = vtkMRMLMarkupsROINode::SafeDownCast(markupsNode);

  bool success = true;

  if (markupObject.HasMember("roiType"))
    {
    rapidjson::Value& roiTypeItem = markupObject["roiType"];
    std::string roiType = roiTypeItem.GetString();
    roiNode->SetROIType(roiNode->GetROITypeFromString(roiType.c_str()));
    }

  if (markupObject.HasMember("origin"))
    {
    rapidjson::Value& originItem = markupObject["origin"];
    double origin_Local[3] = { 0.0, 0.0, 0.0 };
    success &= this->ReadVector(originItem, origin_Local);
    roiNode->SetOrigin(origin_Local);
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
    roiToLocalMatrix->SetElement(0, i, xAxis_Local[i]);
    roiToLocalMatrix->SetElement(1, i, yAxis_Local[i]);
    roiToLocalMatrix->SetElement(2, i, zAxis_Local[i]);
    }
  roiNode->GetROIToLocalMatrix()->DeepCopy(roiToLocalMatrix);

  return success;
}

//----------------------------------------------------------------------------
int vtkMRMLMarkupsROIJsonStorageNode::ReadDataInternal(vtkMRMLNode *refNode)
{
  if (!refNode)
    {
    vtkErrorMacro("ReadDataInternal: null reference node!");
    return 0;
    }

  const char* filePath = this->GetFileName();
  if (!filePath)
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: invalid filename");
    return 0;
    }
  rapidjson::Document* jsonRoot = this->Internal->CreateJsonDocumentFromFile(filePath);
  if (!jsonRoot)
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: error opening the file '" << filePath);
    return 0;
    }


  rapidjson::Value& markups = (*jsonRoot)["markups"];
  if (!markups.IsArray() || markups.GetArray().Size() < 1)
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: cannot read " << refNode->GetClassName()
      << " markup from file " << filePath << " (does not contain valid 'markups' array)");
    return 0;
    }

  rapidjson::Value& markup = markups.GetArray()[0];
  std::string className = this->Internal->GetMarkupsClassNameFromJsonValue(markup);
  if (className != refNode->GetClassName())
    {
    vtkErrorMacro("vtkMRMLMarkupsJsonStorageNode::ReadDataInternal failed: cannot read " << refNode->GetClassName()
      << " markups class from file " << filePath << " (it contains " << className << ")");
    return 0;
    }

  vtkMRMLMarkupsNode* markupsNode = vtkMRMLMarkupsNode::SafeDownCast(refNode);
  bool success = this->Internal->UpdateMarkupsNodeFromJsonValue(markupsNode, markup);

  this->Modified();
  delete jsonRoot;
  return success ? 1 : 0;
}

//----------------------------------------------------------------------------
int vtkMRMLMarkupsROIJsonStorageNode::WriteDataInternal(vtkMRMLNode *refNode)
{
  std::string fullName = this->GetFullNameFromFileName();
  if (fullName.empty())
    {
    vtkErrorMacro("vtkMRMLMarkupsROIJsonStorageNode: File name not specified");
    return 0;
    }
  vtkDebugMacro("WriteDataInternal: have file name " << fullName.c_str());

  // cast the input node
  vtkMRMLMarkupsROINode* markupsNode = vtkMRMLMarkupsROINode::SafeDownCast(refNode);
  if (markupsNode == nullptr)
    {
    vtkErrorMacro("WriteData: unable to cast input node " << refNode->GetID() << " to a known markups node");
    return 0;
    }

  // open the file for writing
  FILE* fp = fopen(fullName.c_str(), "wb");
  if (!fp)
    {
    vtkErrorMacro("WriteData: unable to open file " << fullName.c_str() << " for writing");
    return 0;
    }

  // Prepare JSON writer and output stream.
  char writeBuffer[65536];
  rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
  rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);

  bool success = true;
  writer.StartObject();
  writer.Key("@schema"); writer.String(MARKUPS_SCHEMA.c_str());

  writer.Key("markups");
  writer.StartArray();

  writer.StartObject();
  success = success && this->Internal->WriteBasicProperties(writer, markupsNode);
  success = success && this->Internal->WriteControlPoints(writer, markupsNode);
  success = success && this->Internal->WriteMeasurements(writer, markupsNode);
  success = success && this->Internal->WriteROIProperties(writer, markupsNode);

  if (success)
    {
    vtkMRMLMarkupsDisplayNode* displayNode = vtkMRMLMarkupsDisplayNode::SafeDownCast(markupsNode->GetDisplayNode());
    if (displayNode)
      {
      success = success && this->Internal->WriteDisplayProperties(writer, displayNode);
      }
    }
  writer.EndObject();

  writer.EndArray();

  writer.EndObject();
  fclose(fp);
  return (success ? 1 : 0);
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROIJsonStorageNode::InitializeSupportedReadFileTypes()
{
  this->SupportedReadFileTypes->InsertNextValue("Markups JSON (.mrk.json)");
  this->SupportedReadFileTypes->InsertNextValue("Markups JSON (.json)");
}

//----------------------------------------------------------------------------
void vtkMRMLMarkupsROIJsonStorageNode::InitializeSupportedWriteFileTypes()
{
  this->SupportedWriteFileTypes->InsertNextValue("Markups JSON (.mrk.json)");
  this->SupportedWriteFileTypes->InsertNextValue("Markups JSON (.json)");
}
