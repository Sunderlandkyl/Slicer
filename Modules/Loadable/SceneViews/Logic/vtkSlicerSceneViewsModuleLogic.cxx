// SlicerLogic includes
#include "vtkSlicerSceneViewsModuleLogic.h"

// Sequences logic includes
#include <vtkSlicerSequencesLogic.h>

// Sequences MRML includes
#include <vtkMRMLSequenceNode.h>

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

const int MAXIMUM_BATCH_PROCESSING_NODES = 25;

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
  bool saveDisplayNodes/*=true*/ , bool saveViewNodes/*=true*/)
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

    std::vector<vtkMRMLNode*> volumePropertyNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLVolumePropertyNode", volumePropertyNodes);
    savedNodes.insert(savedNodes.end(), volumePropertyNodes.begin(), volumePropertyNodes.end());

    std::vector<vtkMRMLNode*> clipNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLClipNode", clipNodes);
    savedNodes.insert(savedNodes.end(), clipNodes.begin(), clipNodes.end());

    std::vector<vtkMRMLNode*> crosshairNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLCrosshairNode", crosshairNodes);
    savedNodes.insert(savedNodes.end(), crosshairNodes.begin(), crosshairNodes.end());
  }

  if (saveViewNodes)
  {
    std::vector<vtkMRMLNode*> viewNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLAbstractViewNode", viewNodes);
    savedNodes.insert(savedNodes.end(), viewNodes.begin(), viewNodes.end());

    std::vector<vtkMRMLNode*> cameraNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLCameraNode", cameraNodes);
    savedNodes.insert(savedNodes.end(), cameraNodes.begin(), cameraNodes.end());

    std::vector<vtkMRMLNode*> sliceCompositeNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLSliceCompositeNode", sliceCompositeNodes);
    savedNodes.insert(savedNodes.end(), sliceCompositeNodes.begin(), sliceCompositeNodes.end());

    std::vector<vtkMRMLNode*> layoutNodes;
    this->GetMRMLScene()->GetNodesByClass("vtkMRMLLayoutNode", layoutNodes);
    savedNodes.insert(savedNodes.end(), layoutNodes.begin(), layoutNodes.end());
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

  vtkMRMLVolumeNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (screenshotNode)
  {
    screenshotNode->SetAndObserveImageData(screenshot);
    savedNodes.push_back(screenshotNode);
  }

  if (savedNodes.size() > MAXIMUM_BATCH_PROCESSING_NODES)
  {
    this->GetMRMLScene()->StartState(vtkMRMLScene::BatchProcessState);
  }

  vtkSlicerSequencesLogic* sequencesLogic = vtkSlicerSequencesLogic::SafeDownCast(this->GetModuleLogic("Sequences"));
  for (vtkMRMLNode* node : savedNodes)
  {
    vtkMRMLSequenceNode* sequenceNode = sequenceBrowser->GetSequenceNode(node);
    if (!sequenceNode)
    {
      sequenceNode = sequencesLogic->AddSynchronizedNode(nullptr, node, sequenceBrowser);
      sequenceNode->SetIndexType(vtkMRMLSequenceNode::TextIndex);
      sequenceBrowser->SetMissingItemMode(sequenceNode, vtkMRMLSequenceBrowserNode::MissingItemIgnore);
    }
    sequenceBrowser->SetRecording(sequenceNode, true);
  }
  sequenceBrowser->SetRecordingActive(wasRecordingActive);
  sequenceBrowser->SaveProxyNodesState();

  int index = sequenceBrowser->GetNumberOfItems() - 1;
  this->SetNthSceneViewName(index, name);
  this->SetNthSceneViewDescription(index, description);
  this->SetNthSceneViewScreenshotType(index, screenshotType);

  if (savedNodes.size() > MAXIMUM_BATCH_PROCESSING_NODES)
  {
    this->GetMRMLScene()->EndState(vtkMRMLScene::BatchProcessState);
  }
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::
         ModifyNthSceneView(int index,
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
vtkMRMLVolumeNode* vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotProxyNode()
{
  vtkMRMLSequenceBrowserNode* sequenceBrowser = this->GetSceneViewSequenceBrowserNode(false);
  if (!sequenceBrowser)
  {
    // No scene view sequence browser node exists, so no scene views are available
    return nullptr;
  }

  vtkMRMLVolumeNode* screenshotNode = vtkMRMLVolumeNode::SafeDownCast(
    sequenceBrowser->GetNodeReference(vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotReferenceRole()));

  return screenshotNode;
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::SetNthSceneViewName(int index, std::string name)
{
  vtkMRMLNode* screenshotProxyNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotProxyNode)
  {
    vtkErrorMacro("SetNthSceneViewName: Failed to get name proxy node.");
    return;
  }

  this->SetNthNodeAttribute(screenshotProxyNode, index, this->GetSceneViewNameAttributeName(), name);
}

//---------------------------------------------------------------------------
std::string vtkSlicerSceneViewsModuleLogic::GetNthSceneViewName(int index)
{
  vtkMRMLNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    // No name node is available
    return nullptr;
  }

  return this->GetNthNodeAttribute(screenshotNode, index, this->GetSceneViewNameAttributeName());
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::SetNthSceneViewDescription(int index, std::string description)
{
  vtkMRMLNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    vtkErrorMacro("SetNthSceneViewName: Failed to get name proxy node.");
    return;
  }

  this->SetNthNodeAttribute(screenshotNode, index, this->GetSceneViewDescriptionAttributeName(), description);
}

//---------------------------------------------------------------------------
std::string vtkSlicerSceneViewsModuleLogic::GetNthSceneViewDescription(int index)
{
  vtkMRMLNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    vtkErrorMacro("SetNthSceneViewName: Failed to get name proxy node.");
    return "";
  }

  return this->GetNthNodeAttribute(screenshotNode, index, this->GetSceneViewDescriptionAttributeName());
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::SetNthSceneViewScreenshotType(int index, int type)
{
  vtkMRMLNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    vtkErrorMacro("SetNthSceneViewName: Failed to get name proxy node.");
    return;
  }

  this->SetNthNodeAttribute(screenshotNode, index, this->GetSceneViewScreenshotTypeAttributeName(), this->GetScreenShotTypeAsString(type));
}

//---------------------------------------------------------------------------
int vtkSlicerSceneViewsModuleLogic::GetNthSceneViewScreenshotType(int index)
{
  vtkMRMLNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    vtkErrorMacro("SetNthSceneViewName: Failed to get name proxy node.");
    return -1;
  }

  std::string screenshotTypeString = this->GetNthNodeAttribute(screenshotNode, index, this->GetSceneViewScreenshotTypeAttributeName());
  return this->GetScreenShotTypeFromString(screenshotTypeString);
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::SetNthSceneViewScreenshot(int index, vtkImageData* screenshot)
{
  vtkMRMLVolumeNode* proxyScreenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!proxyScreenshotNode)
  {
    // No screenshot node is available
    return;
  }

  vtkMRMLVolumeNode* nthVolumeNode = vtkMRMLVolumeNode::SafeDownCast(this->GetNthSceneViewDataNode(index, proxyScreenshotNode));
  if (!nthVolumeNode)
  {
    // No volume node is available
    return;
  }

  nthVolumeNode->SetAndObserveImageData(screenshot);
}

//---------------------------------------------------------------------------
vtkImageData* vtkSlicerSceneViewsModuleLogic::GetNthSceneViewScreenshot(int index)
{
  vtkMRMLSequenceBrowserNode* sequenceBrowser = this->GetSceneViewSequenceBrowserNode(false);
  if (!sequenceBrowser)
  {
    // No scene view sequence browser node exists, so no scene views are available
    return nullptr;
  }

  vtkMRMLVolumeNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    // No screenshot node is available
    return nullptr;
  }

  vtkMRMLSequenceNode* sequenceNode = sequenceBrowser->GetSequenceNode(screenshotNode);
  if (!sequenceNode)
  {
    // No sequence node is available
    return nullptr;
  }

  if (index < 0 || index >= sequenceNode->GetNumberOfDataNodes())
  {
    // Index is out of range
    return nullptr;
  }

  vtkMRMLVolumeNode* volumeNode = vtkMRMLVolumeNode::SafeDownCast(sequenceNode->GetNthDataNode(index));
  if (!volumeNode)
  {
    return nullptr;
  }

  return volumeNode->GetImageData();
}

//---------------------------------------------------------------------------
bool vtkSlicerSceneViewsModuleLogic::RestoreSceneView(int itemNumber)
{
  vtkMRMLSequenceBrowserNode* sceneViewSequenceBrowser = this->GetSceneViewSequenceBrowserNode(false);
  if (!sceneViewSequenceBrowser)
  {
    vtkErrorMacro("RestoreSceneView: Failed to get scene view sequence browser node.");
    return false;
  }

  return this->RestoreSceneView(sceneViewSequenceBrowser, itemNumber);
}

//---------------------------------------------------------------------------
bool vtkSlicerSceneViewsModuleLogic::RestoreSceneView(vtkMRMLSequenceBrowserNode* sequenceBrowser, int itemNumber)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return false;
  }

  if (!sequenceBrowser)
  {
    vtkErrorMacro("RestoreSceneView: Invalid sequence browser node.");
    return false;
  }

  if (itemNumber < 0 || itemNumber >= sequenceBrowser->GetNumberOfItems())
  {
    vtkErrorMacro("RestoreSceneView: Invalid item number.");
    return false;
  }

  if (sequenceBrowser->GetSelectedItemNumber() != itemNumber)
  {
    sequenceBrowser->SetSelectedItemNumber(itemNumber);
  }
  else
  {
    vtkSlicerSequencesLogic* sequencesLogic = vtkSlicerSequencesLogic::SafeDownCast(this->GetModuleLogic("Sequences"));
    sequencesLogic->UpdateProxyNodesFromSequences(sequenceBrowser);
  }

  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerSceneViewsModuleLogic::RemoveSceneView(int index)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return true;
  }

  vtkMRMLSequenceBrowserNode* sequenceBrowser = this->GetSceneViewSequenceBrowserNode(false);
  if (!sequenceBrowser)
  {
    vtkErrorMacro("RemoveSceneView: Invalid sequence browser node.");
    return false;
  }

  if (index < 0 || index >= sequenceBrowser->GetNumberOfItems())
  {
    vtkErrorMacro("RemoveSceneView: Invalid item number.");
    return false;
  }

  vtkMRMLVolumeNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  if (!screenshotNode)
  {
    // No name node is available
    return false;
  }

  if (sequenceBrowser->GetNumberOfItems() == 1)
  {
    // Removing the last item in a sequence will cause an update of the scene nodes.
    // To avoid this, remove the sequence browser and all synchronized sequence nodes.
    // If a scene view is added later, then a new sequence browser will be created.
    std::vector<vtkMRMLSequenceNode*> sequenceNodes;
    sequenceBrowser->GetSynchronizedSequenceNodes(sequenceNodes, true);
    this->GetMRMLScene()->RemoveNode(sequenceBrowser);
    for (vtkMRMLSequenceNode* sequenceNode : sequenceNodes)
    {
      this->GetMRMLScene()->RemoveNode(sequenceNode);
    }
    return true;
  }

  vtkMRMLSequenceNode* sequenceNode = sequenceBrowser->GetSequenceNode(screenshotNode);
  if (!sequenceNode)
  {
    // No sequence node is available
    return false;
  }

  std::vector<MRMLNodeModifyBlocker> blockers;
  blockers.emplace_back(sequenceBrowser);

  std::string value = sequenceNode->GetNthIndexValue(index);

  std::vector<vtkMRMLSequenceNode*> sequenceNodes;
  sequenceBrowser->GetSynchronizedSequenceNodes(sequenceNodes, true);

  for (vtkMRMLSequenceNode* sequenceNode : sequenceNodes)
  {
    blockers.emplace_back(sequenceNode);
  }
  for (vtkMRMLSequenceNode* sequenceNode : sequenceNodes)
  {
    if (sequenceNode->GetDataNodeAtValue(value))
    {
      sequenceNode->RemoveDataNodeAtValue(value);
    }
  }

  return true;
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
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewNameAttributeName()
{
  return "SceneViewName";
}

//-----------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewDescriptionAttributeName()
{
  return "SceneViewDescription";
}

//-----------------------------------------------------------------------------
const char* vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotTypeAttributeName()
{
  return "ScreenshotType";
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
    sequenceBrowserNode->SetIndexDisplayMode(vtkMRMLSequenceBrowserNode::IndexDisplayAsIndex);

    vtkSlicerSequencesLogic* sequencesLogic = vtkSlicerSequencesLogic::SafeDownCast(this->GetModuleLogic("Sequences"));

    vtkSmartPointer<vtkMRMLVectorVolumeNode> screenshotNode = vtkSmartPointer<vtkMRMLVectorVolumeNode>::Take(
      vtkMRMLVectorVolumeNode::SafeDownCast(this->GetMRMLScene()->CreateNodeByClass("vtkMRMLVectorVolumeNode")));
    screenshotNode->SetName(this->GetMRMLScene()->GetUniqueNameByString("SceneViewScreenshot"));
    this->GetMRMLScene()->AddNode(screenshotNode);
    sequencesLogic->AddSynchronizedNode(nullptr, screenshotNode, sequenceBrowserNode);
    sequenceBrowserNode->AddNodeReferenceID(vtkSlicerSceneViewsModuleLogic::GetSceneViewScreenshotReferenceRole(), screenshotNode->GetID());
  }

  return sequenceBrowserNode;
}

//---------------------------------------------------------------------------
std::string vtkSlicerSceneViewsModuleLogic::GetScreenShotTypeAsString(int type)
{
  switch (type)
  {
  case ScreenShotType3D:
    return "3D";
  case ScreenShotTypeRed:
    return "Red";
  case ScreenShotTypeYellow:
    return "Yellow";
  case ScreenShotTypeGreen:
    return "Green";
  case ScreenShotTypeFullLayout:
    return "FullLayout";
  default:
    return "Unknown";
  }
}

//---------------------------------------------------------------------------
int vtkSlicerSceneViewsModuleLogic::GetScreenShotTypeFromString(const std::string& type)
{
  if (type == this->GetScreenShotTypeAsString(ScreenShotType3D))
  {
    return ScreenShotType3D;
  }
  if (type == this->GetScreenShotTypeAsString(ScreenShotTypeRed))
  {
    return ScreenShotTypeRed;
  }
  if (type == this->GetScreenShotTypeAsString(ScreenShotTypeYellow))
  {
    return ScreenShotTypeYellow;
  }
  if (type == this->GetScreenShotTypeAsString(ScreenShotTypeGreen))
  {
    return ScreenShotTypeGreen;
  }
  if (type == this->GetScreenShotTypeAsString(ScreenShotTypeFullLayout))
  {
    return ScreenShotTypeFullLayout;
  }
  return -1;
}

//---------------------------------------------------------------------------
vtkMRMLNode* vtkSlicerSceneViewsModuleLogic::GetNthSceneViewDataNode(int index, vtkMRMLNode* proxyNode)
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return nullptr;
  }

  vtkMRMLSequenceBrowserNode* sequenceBrowser = this->GetSceneViewSequenceBrowserNode(false);
  if (!sequenceBrowser)
  {
    vtkErrorMacro("GetNthSceneViewDataNode: Failed to get scene view sequence browser node.");
    return nullptr;
  }

  vtkMRMLSequenceNode* sequenceNode = sequenceBrowser->GetSequenceNode(proxyNode);
  if (!sequenceNode)
  {
    vtkErrorMacro("GetNthSceneViewDataNode: Failed to get sequence node.");
    return nullptr;
  }

  if (index < 0 || index >= sequenceNode->GetNumberOfDataNodes())
  {
    vtkErrorMacro("GetNthSceneViewDataNode: Invalid index.");
    return nullptr;
  }

  return sequenceNode->GetNthDataNode(index);
}

//---------------------------------------------------------------------------
void vtkSlicerSceneViewsModuleLogic::SetNthNodeAttribute(vtkMRMLNode* proxyNode, int index, std::string attributeName, std::string attributeValue)
{
  if (!proxyNode)
  {
    vtkErrorMacro("SetNthNodeAttribute: Failed to get proxy node.");
    return;
  }

  vtkMRMLNode* node = this->GetNthSceneViewDataNode(index, proxyNode);
  if (!node)
  {
    vtkErrorMacro("SetNthNodeAttribute: Failed to get data node.");
    return;
  }

  node->SetAttribute(attributeName.c_str(), attributeValue.c_str());
}

//---------------------------------------------------------------------------
std::string vtkSlicerSceneViewsModuleLogic::GetNthNodeAttribute(vtkMRMLNode* proxyNode, int index, std::string attributeName)
{
  if (!proxyNode)
  {
    vtkErrorMacro("GetNthNodeAttribute: Failed to get proxy node.");
    return "";
  }

  vtkMRMLNode* node = this->GetNthSceneViewDataNode(index, proxyNode);
  if (!node)
  {
    vtkErrorMacro("GetNthNodeAttribute: Failed to get data node.");
    return "";
  }

  const char* attributeValue = node->GetAttribute(attributeName.c_str());
  if (!attributeValue)
  {
    return "";
  }

  return attributeValue;
}

//---------------------------------------------------------------------------
int vtkSlicerSceneViewsModuleLogic::GetNumberOfSceneViews()
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("No scene set.");
    return 0;
  }

  vtkMRMLSequenceBrowserNode* sequenceBrowser = this->GetSceneViewSequenceBrowserNode(false);
  if (!sequenceBrowser)
  {
    return 0;
  }

  vtkMRMLNode* screenshotNode = this->GetSceneViewScreenshotProxyNode();
  vtkMRMLSequenceNode* sequenceNode = sequenceBrowser->GetSequenceNode(screenshotNode);
  if (!sequenceNode)
  {
    return 0;
  }

  return sequenceNode->GetNumberOfDataNodes();
}
