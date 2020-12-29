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
and was supported through CANARIE's Research Software Program, and Cancer
Care Ontario.

==============================================================================*/

// MRML includes
#include "vtkMRMLCoreTestingMacros.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSegmentationNode.h"
#include "vtkMRMLSegmentationStorageNode.h"
#include "vtkSegmentationConverterFactory.h"

// Converter rules
#include "vtkClosedSurfaceToBinaryLabelmapConversionRule.h"
#include "vtkBinaryLabelmapToClosedSurfaceConversionRule.h"

#include "vtkFractionalLabelmapToClosedSurfaceConversionRule.h"
#include "vtkClosedSurfaceToFractionalLabelmapConversionRule.h"

int vtkMRMLSegmentationStorageNodeTest2(int argc, char * argv[] )
{
  vtkNew<vtkMRMLScene> scene;

  //vtkSegmentationConverterFactory* converterFactory = vtkSegmentationConverterFactory::GetInstance();
  //converterFactory->RegisterConverterRule(vtkSmartPointer<vtkClosedSurfaceToBinaryLabelmapConversionRule>::New());
  //converterFactory->RegisterConverterRule(vtkSmartPointer<vtkBinaryLabelmapToClosedSurfaceConversionRule>::New());
  //converterFactory->RegisterConverterRule(vtkSmartPointer<vtkFractionalLabelmapToClosedSurfaceConversionRule>::New());
  //converterFactory->RegisterConverterRule(vtkSmartPointer<vtkClosedSurfaceToFractionalLabelmapConversionRule>::New());

  //const char* outputLabelmapExport "output.seg.nrrd";

  //std::cout << "Testing writing shared labelmap" << std::endl;
  //{
  //  vtkNew<vtkMRMLSegmentationNode> segmentationNode;
  //  scene->AddNode(segmentationNode);
  //  vtkNew<vtkMRMLSegmentationStorageNode> segmentationStorageNode;
  //  scene->AddNode(segmentationStorageNode);
  //  segmentationStorageNode->SetFileName(slicerSegmentationFilename);
  //  segmentationStorageNode->ReadData(segmentationNode);
  //  vtkSegmentation* segmentation = segmentationNode->GetSegmentation();
  //  CHECK_NOT_NULL(segmentation);

  //  int numberOfSegments = segmentation->GetNumberOfSegments();
  //  CHECK_INT(numberOfSegments, 3);

  //  int numberOfLayers = segmentation->GetNumberOfLayers(vtkSegmentationConverter::GetBinaryLabelmapRepresentationName());
  //  CHECK_INT(numberOfLayers, 2);

  //  scene->Clear();
  //}

  return EXIT_SUCCESS;
}
