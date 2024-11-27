// SlicerLogic includes
#include "vtkSlicerSceneViewsModuleLogic.h"

// Sequences logic includes
#include <vtkSlicerSequencesLogic.h>

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLSequenceNode.h>
#include <vtkMRMLSequenceBrowserNode.h>
#include <vtkMRMLTextNode.h>
#include <vtkMRMLVectorVolumeNode.h>

// VTK includes
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkSmartPointer.h>

// STD includes
#include <iostream>
#include <sstream>

//-----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerSceneViewsModuleLogic);

//-----------------------------------------------------------------------------
// vtkSlicerSceneViewsModuleLogic methods
//-----------------------------------------------------------------------------
vtkSlicerSceneViewsModuleLogic::vtkSlicerSceneViewsModuleLogic() = default;

//-----------------------------------------------------------------------------
vtkSlicerSceneViewsModuleLogic::~vtkSlicerSceneViewsModuleLogic() = default;

//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkDebugMacro("SetMRMLSceneInternal - listening to scene events");

  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::EndCloseEvent);
  events->InsertNextValue(vtkMRMLScene::EndImportEvent);
  events->InsertNextValue(vtkMRMLScene::EndRestoreEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::OnMRMLSceneNodeAdded(vtkMRMLNode* vtkNotUsed(node))
{
  vtkDebugMacro("OnMRMLSceneNodeAddedEvent");
}

//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::OnMRMLSceneEndImport()
{
  vtkDebugMacro("OnMRMLSceneEndImport");
}

//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::OnMRMLSceneEndRestore()
{
  vtkDebugMacro("OnMRMLSceneEndRestore");
}

//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::OnMRMLNodeModified(vtkMRMLNode* vtkNotUsed(node))
{
}

//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::OnMRMLSceneEndClose()
{
}


//-----------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::RegisterNodes()
{
  if (!this->GetMRMLScene())
  {
    std::cerr << "RegisterNodes: no scene on which to register nodes" << std::endl;
    return;
  }
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::CreateSceneView(const char* name, const char* description, int screenshotType, vtkImageData* screenshot,
  bool saveDisplayNodes/*=true*/ , bool saveViewNodes/*=true*/, bool saveCameraNodes/*=true*/)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return;
  }

  std::vector<vtkMRMLNode*> savedNodes;
  if (saveDisplayNodes)
  {
    std::vector<vtkMRMLNode*> displayNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLDisplayNode", displayNodes);
    savedNodes.insert(savedNodes.end(), displayNodes.begin(), displayNodes.end());
  }
  if (saveViewNodes)
  {
    std::vector<vtkMRMLNode*> viewNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLAbstractViewNode", viewNodes);
    savedNodes.insert(savedNodes.end(), viewNodes.begin(), viewNodes.end());
  }
  if (saveCameraNodes)
  {
    std::vector<vtkMRMLNode*> cameraNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLCameraNode", cameraNodes);
    savedNodes.insert(savedNodes.end(), cameraNodes.begin(), cameraNodes.end());
  }
  this->CreateSceneView(name, description, screenshotType, screenshot, savedNodes);
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::CreateSceneView(const char* name, const char* description, int screenshotType, vtkImageData* screenshot,
  vtkCollection* savedNodes)
{
  std::vector<vtkMRMLNode*> savedNodesVector;
  for (int i = 0; i < savedNodes->GetNumberOfItems(); ++i)
  {
    vtkMRMLNode* node = vtkMRMLNode::SafeDownCast(savedNodes->GetItemAsObject(i));
    if (node)
    {
      savedNodesVector.push_back(node);
    }
  }
  this->CreateSceneView(name, description, screenshotType, screenshot, savedNodesVector);
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::CreateSceneView(const char* name, const char* description, int screenshotType, vtkImageData* screenshot,
  std::vector<vtkMRMLNode*> savedNodes)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return;
  }

  if (!screenshot)
  {
    vtkErrorMacro("CreateSceneView: No screenshot was set.");
    return;
  }

  vtkMRMLSequenceBrowserNode* sequenceBrowser = this->GetSceneViewSequenceBrowserNode(true);
  if (!sequenceBrowser)
  {
    vtkErrorMacro("CreateSceneView: Failed to get or create sequence browser node.");
    return;
  }

  MRMLNodeModifyBlocker blocker(sequenceBrowser);

  bool wasRecordingActive = sequenceBrowser->GetRecordingActive();
  sequenceBrowser->RecordingActiveOn();

  std::vector<vtkMRMLSequenceNode*> sequenceNodes;
  sequenceBrowser->GetSynchronizedSequenceNodes(sequenceNodes, true);
  for (vtkMRMLSequenceNode* sequenceNode : sequenceNodes)
  {
    sequenceBrowser->SetRecording(sequenceNode, false);
  }

  vtkMRMLTextNode* descriptionNode = vtkMRMLTextNode::SafeDownCast(
    sequenceBrowser->GetNodeReference(vtkSlicerSceneViewsModuleLogic::GetSceneViewDescriptionReferenceRole()));
  if (descriptionNode)
  {
    descriptionNode->SetText(description);
    savedNodes.push_back(descriptionNode);
  }

  vtkMRMLVectorVolumeNode* screenshotNode = vtkMRMLVectorVolumeNode::SafeDownCast(
    sequenceBrowser->GetNodeReference(vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotReferenceRole()));
  if (screenshotNode)
  {
    screenshotNode->SetAndObserveImageData(screenshot);
    savedNodes.push_back(screenshotNode);
  }

  vtkSlicerSequencesLogic* sequencesLogic = vtkSlicerSequencesLogic::SafeDownCast(this->GetModuleLogic("Sequences"));
  for (vtkMRMLNode* node : savedNodes)
  {
    vtkMRMLSequenceNode* sequenceNode = sequenceBrowser->GetSequenceNode(node);
    if (!sequenceNode)
    {
      sequenceNode = sequencesLogic->AddSynchronizedNode(nullptr, node, sequenceBrowser);
      sequenceBrowser->SetMissingItemMode(sequenceNode, vtkMRMLSequenceBrowserNode::MissingItemSetToDefault);
    }
    sequenceBrowser->SetRecording(sequenceNode, true);
  }

  sequenceBrowser->SetRecordingActive(wasRecordingActive);
  sequenceBrowser->SaveProxyNodesState();
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::
         ModifySceneView(std::string id,
                         const char* name,
                         const char* description,
                         int vtkNotUsed(screenshotType),
                         vtkImageData* screenshot)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return;
  }

  if (!screenshot)
  {
    vtkErrorMacro("ModifySceneView: No screenshot was set.");
    return;
  }
}

//---------------------------------------------------------------------------
std::string vtkSlicerSceneViewsModuleLogic::GetSceneViewName(const char* id)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return nullptr;
  }

  return "";
}

//---------------------------------------------------------------------------
std::string vtkSlicerSceneViewsModuleLogic::GetSceneViewDescription(const char* id)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return nullptr;
  }

  return "";
}

//---------------------------------------------------------------------------
int vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotType(const char* id)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return -1;
  }

  return -1;
}

//---------------------------------------------------------------------------
vtkImageData* vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshot(const char* id)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return nullptr;
  }

  return nullptr;
}

//---------------------------------------------------------------------------
bool vtkSlicerSceneViewsModuleLogic::RestoreSceneView(const char* id, bool removeNodes)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return true;
  }

  return false;
}

//---------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::MoveSceneViewUp(const char* vtkNotUsed(id))
{
  // reset stringHolder
  this->m_StringHolder = "";

  vtkErrorMacro("MoveSceneViewUp: operation not supported!");
  return this->m_StringHolder.c_str();
}

//---------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::MoveSceneViewDown(const char* vtkNotUsed(id))
{
  // reset stringHolder
  this->m_StringHolder = "";

  vtkErrorMacro("MoveSceneViewDown: operation not supported!");
  return this->m_StringHolder.c_str();
}

//-----------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewNodeAttributeName()
{
  return "SceneView";
}

//-----------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewNodeAttributeValue()
{
  return "SceneView";
}

//-----------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewDescriptionReferenceRole()
{
  return "SceneViewDescription";
}

//-----------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotReferenceRole()
{
  return "SceneViewScreenshot";
}

//---------------------------------------------------------------------------
vtkMRMLSequenceBrowserNode* vtkSlicerSceneViewsModuleLogic::GetSceneViewSequenceBrowserNode(bool addMissingNodes)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return nullptr;
  }

  std::vector<vtkMRMLNode*> nodes;
  this->GetMRMLScene()->GetNodesByClass("vtkMRMLSequenceBrowserNode", nodes);

  for (vtkMRMLNode* node : nodes)
  {
    vtkMRMLSequenceBrowserNode* sequenceBrowserNode = vtkMRMLSequenceBrowserNode::SafeDownCast(node);
    if (!sequenceBrowserNode)
    {
      continue;
    }

    const char* attributeValue = sequenceBrowserNode->GetAttribute(vtkSlicerSceneViewsModuleLogic::GetSceneViewNodeAttributeName());
    if (attributeValue && strcmp(attributeValue, vtkSlicerSceneViewsModuleLogic::GetSceneViewNodeAttributeValue()) == 0)
    {
      return sequenceBrowserNode;
    }
  }

  vtkMRMLSequenceBrowserNode* sequenceBrowserNode = nullptr;
  if (addMissingNodes)
  {
    sequenceBrowserNode = vtkMRMLSequenceBrowserNode::SafeDownCast(this->GetMRMLScene()->AddNewNodeByClass("vtkMRMLSequenceBrowserNode", "SceneView"));
    sequenceBrowserNode->SetAttribute(
      vtkSlicerSceneViewsModuleLogic::GetSceneViewNodeAttributeName(),
      vtkSlicerSceneViewsModuleLogic::GetSceneViewNodeAttributeValue());

    vtkSlicerSequencesLogic* sequencesLogic = vtkSlicerSequencesLogic::SafeDownCast(this->GetModuleLogic("Sequences"));

    vtkSmartPointer<vtkMRMLTextNode> textNode = vtkSmartPointer<vtkMRMLTextNode>::Take(
      vtkMRMLTextNode::SafeDownCast(this->GetMRMLScene()->CreateNodeByClass("vtkMRMLTextNode")));
    //textNode->SetHideFromEditors(true); // Determine if we want to hide this node from the user
    textNode->SetText("SceneViewDescription");
    textNode->SetName(this->GetMRMLScene()->GetUniqueNameByString("SceneViewDescription"));
    this->GetMRMLScene()->AddNode(textNode);

    sequencesLogic->AddSynchronizedNode(nullptr, textNode, sequenceBrowserNode);
    sequenceBrowserNode->AddNodeReferenceID(vtkSlicerSceneViewsModuleLogic::GetSceneViewDescriptionReferenceRole(), textNode->GetID());

    vtkSmartPointer<vtkMRMLVectorVolumeNode> screenshotNode = vtkSmartPointer<vtkMRMLVectorVolumeNode>::Take(
      vtkMRMLVectorVolumeNode::SafeDownCast(this->GetMRMLScene()->CreateNodeByClass("vtkMRMLVectorVolumeNode")));
    //screenshotNode->SetHideFromEditors(true); // Determine if we want to hide this node from the user
    screenshotNode->SetName(this->GetMRMLScene()->GetUniqueNameByString("SceneViewScreenshot"));
    this->GetMRMLScene()->AddNode(screenshotNode);

    sequencesLogic->AddSynchronizedNode(nullptr, screenshotNode, sequenceBrowserNode);
    sequenceBrowserNode->AddNodeReferenceID(vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotReferenceRole(), screenshotNode->GetID());
  }

  return sequenceBrowserNode;
}
