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

  This file was originally developed by Julien Finet, Kitware Inc.
  and was partially funded by NIH grant 3P41RR013218-12S1

==============================================================================*/

// MRML includes
#include "vtkMRMLCoreTestingMacros.h"
#include "vtkMRMLScalarVolumeDisplayNode.h"
#include "vtkMRMLScalarVolumeNode.h"
#include "vtkMRMLScene.h"

// VTK includes
#include <vtkNew.h>
#include <vtkSmartPointer.h>

namespace
{

int populateScene(vtkMRMLScene* scene, bool saveInSceneView)
{
  vtkNew<vtkMRMLSceneViewNode> sceneViewtoRegister;
  scene->RegisterNodeClass(sceneViewtoRegister.GetPointer());

  vtkNew<vtkMRMLScalarVolumeNode> displayableNode;
  scene->AddNode(displayableNode.GetPointer());

  vtkNew<vtkMRMLScalarVolumeDisplayNode> displayNode;
  scene->AddNode(displayNode.GetPointer());

  displayableNode->SetAndObserveDisplayNodeID(displayNode->GetID());

  if (saveInSceneView)
  {
    vtkNew<vtkMRMLSceneViewNode> sceneViewNode;
    scene->AddNode(sceneViewNode.GetPointer());

    sceneViewNode->StoreScene();
  }

  scene->RemoveNode(displayableNode.GetPointer());
  return EXIT_SUCCESS;
}

} // end of anonymous namespace

//---------------------------------------------------------------------------
int vtkMRMLSceneViewNodeImportSceneTest(int vtkNotUsed(argc),
                                       char * vtkNotUsed(argv)[] )
{
  return EXIT_SUCCESS;
}
