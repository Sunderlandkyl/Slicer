/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Kitware Inc.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Daniel Haehn, UPenn
  and was partially funded by NIH grant 3P41RR013218-12S1

==============================================================================*/

#ifndef vtkSlicerSceneViewsModuleLogic_h
#define vtkSlicerSceneViewsModuleLogic_h

// SlicerLogic includes
#include "vtkSlicerBaseLogic.h"

// MRMLLogic includes
#include "vtkMRMLAbstractLogic.h"

#include "vtkSlicerSceneViewsModuleLogicExport.h"
//#include "qSlicerSceneViewsModuleExport.h"

#include "vtkSlicerModuleLogic.h"

// MRML includes
class vtkMRMLSequenceBrowserNode;
class vtkMRMLSceneViewNode;
class vtkMRMLTextNode;
class vtkMRMLVolumeNode;

// VTK includes
class vtkImageData;

// STD includes
#include <string>

class VTK_SLICER_SCENEVIEWS_MODULE_LOGIC_EXPORT vtkSlicerSceneViewsModuleLogic :
  public vtkSlicerModuleLogic
{
public:

  static vtkSlicerSceneViewsModuleLogic* New();
  vtkTypeMacro(vtkSlicerSceneViewsModuleLogic, vtkSlicerModuleLogic);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Initialize listening to MRML events
  void SetMRMLSceneInternal(vtkMRMLScene* newScene) override;

  /// Register MRML Node classes to Scene. Gets called automatically when the MRMLScene is attached to this logic class.
  void RegisterNodes() override;

  /// Create a sceneView..
  void CreateSceneView(const char* name, const char* description, int screenshotType, vtkImageData* screenshot,
    bool saveDisplayNodes = true, bool saveViewNodes = true, vtkMRMLSequenceBrowserNode* sequenceBrowser=nullptr);
  void CreateSceneView(const char* name, const char* description, int screenshotType, vtkImageData* screenshot,
    vtkCollection* savedNodes, vtkMRMLSequenceBrowserNode* sequenceBrowser=nullptr);
  void CreateSceneView(const char* name, const char* description, int screenshotType, vtkImageData* screenshot,
    std::vector<vtkMRMLNode*> savedNodes, vtkMRMLSequenceBrowserNode* sequenceBrowser=nullptr);

  bool RestoreSceneView(int sceneViewIndex);

  /// Modify an existing sceneView.
  void ModifyNthSceneView(int sceneViewIndex, const char* name, const char* description, int screenshotType, vtkImageData* screenshot);

  int SceneViewIndexToSequenceBrowserIndex(int sceneViewIndex);

  //@{
  /// Set/Get the name of an existing sceneView.
  void SetNthSceneViewName(int index, std::string name);
  std::string GetNthSceneViewName(int index);
  //@}

  /// Get the number of sceneViews.
  int GetNumberOfSceneViews();

  //@{
  /// Set/Get the description of an existing sceneView.
  void SetNthSceneViewDescription(int index, std::string description);
  std::string GetNthSceneViewDescription(int index);
  //@}

  //@{
  /// Set/Get the screenshot type of an existing sceneView.
  void SetNthSceneViewScreenshotType(int index, int type);
  int GetNthSceneViewScreenshotType(int index);
  //@}

  //@{
  /// Set/Get the screenshot of an existing sceneView.
  void SetNthSceneViewScreenshot(int index, vtkImageData* screenshot);
  vtkImageData* GetNthSceneViewScreenshot(int index);
  //@}

  /// Restore a sceneView.
  /// If removeNodes flag is false, don't restore the scene if it will remove data.
  /// The method will return with false if restore failed because nodes were not allowed
  /// to be removed.

  /// Remove a sceneView.
  bool RemoveSceneView(int index);

  static const char* GetSceneViewNodeAttributeName();
  static const char* GetSceneViewNodeAttributeValue();
  static const char* GetSceneViewNameAttributeName();
  static const char* GetSceneViewDescriptionAttributeName();
  static const char* GetSceneViewScreenshotTypeAttributeName();

  static const char* GetSceneViewScreenshotReferenceRole();

  vtkMRMLSequenceBrowserNode* GetSceneViewSequenceBrowserNode(bool addMissingNode);
  vtkMRMLSequenceBrowserNode* GetNthSceneViewSequenceBrowserNode(int index);
  vtkMRMLSequenceBrowserNode* CreateSceneViewSequenceBrowserNode();

  /// The screenshot type of a sceneView
  enum
  {
    ScreenShotType3D = 0,
    ScreenShotTypeRed = 1,
    ScreenShotTypeYellow = 2,
    ScreenShotTypeGreen = 3,
    ScreenShotTypeFullLayout = 4
  };
  std::string GetScreenShotTypeAsString(int type);
  int GetScreenShotTypeFromString(const std::string& type);

protected:

  vtkSlicerSceneViewsModuleLogic();

  ~vtkSlicerSceneViewsModuleLogic() override;

  void OnMRMLSceneNodeAdded(vtkMRMLNode* node) override;
  void OnMRMLSceneEndImport() override;
  void OnMRMLSceneEndRestore() override;
  void OnMRMLSceneEndClose() override;

  void OnMRMLNodeModified(vtkMRMLNode* node) override;

  vtkMRMLVolumeNode* GetSceneViewScreenshotProxyNode(vtkMRMLSequenceBrowserNode* sequenceBrowser=nullptr);
  vtkMRMLNode* GetNthSceneViewDataNode(int index, vtkMRMLNode* proxyNode);

  vtkMRMLVolumeNode* GetNthSceneViewScreenshotDataNode(int index);
  vtkMRMLVolumeNode* GetNthSceneViewScreenshotProxyNode(int index);

  void SetNthNodeAttribute(vtkMRMLNode* proxyTextNode, int index, std::string attributeName, std::string text);
  std::string GetNthNodeAttribute(vtkMRMLNode* proxyTextNode, int index, std::string attributeName);

  void ConvertSceneViewNodesToSequenceBrowserNodes(vtkMRMLScene* scene);
  vtkMRMLSequenceBrowserNode* ConvertSceneViewNodeToSequenceBrowserNode(vtkMRMLSceneViewNode* sceneView, vtkMRMLSequenceBrowserNode* sequenceBrowserNode);

private:

  std::string m_StringHolder;

private:
  vtkSlicerSceneViewsModuleLogic(const vtkSlicerSceneViewsModuleLogic&) = delete;
  void operator=(const vtkSlicerSceneViewsModuleLogic&) = delete;
};

#endif
