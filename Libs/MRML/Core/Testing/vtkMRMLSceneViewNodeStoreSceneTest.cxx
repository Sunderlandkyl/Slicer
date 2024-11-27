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
#include <vtkCollection.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkTimerLog.h>

namespace
{

  void populateScene(vtkMRMLScene* scene);
  int store();
  int storeAndRestore();
  int storeAndRemoveVolume();
  int storeTwice();
  int storeAndRestoreTwice();
  int storeTwiceAndRemoveVolume();
  int references();
  int storePerformance();

} // end of anonymous namespace

//---------------------------------------------------------------------------
int vtkMRMLSceneViewNodeStoreSceneTest(int vtkNotUsed(argc),
  char* vtkNotUsed(argv)[])
{
  CHECK_EXIT_SUCCESS(store());
  CHECK_EXIT_SUCCESS(storeAndRestore());
  CHECK_EXIT_SUCCESS(storeAndRemoveVolume());
  CHECK_EXIT_SUCCESS(storeTwice());
  CHECK_EXIT_SUCCESS(storeAndRestoreTwice());
  CHECK_EXIT_SUCCESS(storeTwiceAndRemoveVolume());
  CHECK_EXIT_SUCCESS(references());
  CHECK_EXIT_SUCCESS(storePerformance());
  return EXIT_SUCCESS;
}

namespace
{

  //---------------------------------------------------------------------------
  void populateScene(vtkMRMLScene* scene)
  {
    vtkNew<vtkMRMLScalarVolumeDisplayNode> displayNode;
    scene->AddNode(displayNode.GetPointer());

    vtkNew<vtkMRMLScalarVolumeNode> volumeNode;
    volumeNode->SetScene(scene);
    scene->AddNode(volumeNode.GetPointer());
    volumeNode->SetAndObserveDisplayNodeID(displayNode->GetID());
  }

  //---------------------------------------------------------------------------
  int store()
  {
    vtkNew<vtkMRMLScene> scene;
    populateScene(scene.GetPointer());

    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int storeAndRestore()
  {
    vtkNew<vtkMRMLScene> scene;
    populateScene(scene.GetPointer());

    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int storeAndRemoveVolume()
  {
    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int storeTwice()
  {
    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int storeAndRestoreTwice()
  {
    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int storeTwiceAndRemoveVolume()
  {
    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int references()
  {
    return EXIT_SUCCESS;
  }

  //---------------------------------------------------------------------------
  int storePerformance()
  {
    return EXIT_SUCCESS;
  }

} // end of anonymous namespace
