/*=auto=========================================================================

  Portions (c) Copyright 2005 Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Program:   3D Slicer
  Module:    $RCSfile: vtkMRMLSceneViewStorageNode.cxx,v $
  Date:      $Date: 2006/03/17 15:10:09 $
  Version:   $Revision: 1.2 $

=========================================================================auto=*/

// MRML includes
#include <vtkArchive.h>
#include "vtkMRMLSceneViewNode.h"
#include "vtkMRMLSceneViewStorageNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSequenceNode.h"
#include "vtkMRMLSequenceStorageNode.h"

// vtksys includes
#include <vtksys/Glob.hxx>

// VTK includes
#include <vtkBMPReader.h>
#include <vtkBMPWriter.h>
#include <vtkErrorCode.h>
#include <vtkImageData.h>
#include <vtkJPEGReader.h>
#include <vtkJPEGWriter.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPNGReader.h>
#include <vtkPNGWriter.h>
#include <vtkSmartPointer.h>
#include <vtkStringArray.h>
#include <vtkTIFFReader.h>
#include <vtkTIFFWriter.h>
#include <vtkVersion.h>

// ITK includes
#include <itksys/SystemTools.hxx>

//----------------------------------------------------------------------------
vtkMRMLNodeNewMacro(vtkMRMLSceneViewStorageNode);

//----------------------------------------------------------------------------
vtkMRMLSceneViewStorageNode::vtkMRMLSceneViewStorageNode()
{
  this->DefaultWriteFileExtension = "svzip";
}

//----------------------------------------------------------------------------
vtkMRMLSceneViewStorageNode::~vtkMRMLSceneViewStorageNode() = default;

//----------------------------------------------------------------------------
void vtkMRMLSceneViewStorageNode::PrintSelf(ostream& os, vtkIndent indent)
{
  return this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
bool vtkMRMLSceneViewStorageNode::CanReadInReferenceNode(vtkMRMLNode *refNode)
{
  return refNode->IsA("vtkMRMLSceneViewNode");
}

//----------------------------------------------------------------------------
int vtkMRMLSceneViewStorageNode::ReadDataInternal(vtkMRMLNode *refNode)
{
  // don't read from disk if restoring
  if (this->GetScene() && this->GetScene()->IsRestoring())
  {
    return 1;
  }

  vtkMRMLSceneViewNode *sceneViewNode = dynamic_cast <vtkMRMLSceneViewNode *> (refNode);

  std::string fullName = this->GetFullNameFromFileName();
  if (fullName.empty())
  {
    vtkErrorMacro("ReadData: File name not specified");
    return 0;
  }

  if (itksys::SystemTools::FileExists(fullName.c_str(), true) == false)
  {
    vtkErrorMacro("ReadDataInternal: file does not exist: " << fullName.c_str());
    return 0;
  }
  // compute file prefix
  std::string extension = vtkMRMLStorageNode::GetLowercaseExtensionFromFileName(fullName);
  if( extension.empty() )
  {
    vtkErrorMacro("ReadData: no file extension specified: " << fullName.c_str());
    return 0;
  }
  vtkDebugMacro("ReadData: extension = " << extension.c_str());

  int result = 1;
  vtkNew<vtkImageData> imageData;
  vtkSmartPointer<vtkImageReader2> reader;

  if ( extension == std::string(".png") )
  {
      reader=vtkSmartPointer<vtkPNGReader>::New();
  }
  else if (extension == std::string(".jpg") ||
           extension == std::string(".jpeg"))
  {
    reader=vtkSmartPointer<vtkJPEGReader>::New();
  }
  else if (extension == std::string(".tiff"))
  {
    reader=vtkSmartPointer<vtkTIFFReader>::New();
  }
  else if (extension == std::string(".bmp"))
  {
    reader=vtkSmartPointer<vtkBMPReader>::New();
  }
  else if (extension == std::string(".svzip"))
  {
    return this->ReadSceneViewMRB(sceneViewNode, fullName.c_str());
  }
  else
  {
    vtkDebugMacro("Cannot read scene view file '" << fullName.c_str() << "' (extension = " << extension.c_str() << ")");
    return 0;
  }

  try
  {
    reader->SetFileName(fullName.c_str());
    reader->Update();
    if (reader->GetOutput())
    {
      vtkDebugMacro("ReadData: read file, copying output to image data");
      imageData->DeepCopy(reader->GetOutput());
    }
    if (reader->GetErrorCode() != vtkErrorCode::NoError)
    {
      vtkDebugMacro("Cannot read scene view file '" << fullName.c_str() << "' ("
        << vtkErrorCode::GetStringFromErrorCode(reader->GetErrorCode()) << ")");
      result = 0;
    }
  }
  catch (...)
  {
    vtkWarningMacro("ReadData: error in read, setting result to 0");
    result = 0;
  }

  sceneViewNode->SetScreenShot(imageData.GetPointer());
  sceneViewNode->GetScreenShot()->SetSpacing(1.0, 1.0, 1.0);
  sceneViewNode->GetScreenShot()->SetOrigin(0.0, 0.0, 0.0);

  return result;
}

//----------------------------------------------------------------------------
int vtkMRMLSceneViewStorageNode::ReadSceneViewMRB(vtkMRMLSceneViewNode* refNode, const char* path)
{
  int result = 1;

  // Unzip the scene view svip file
  std::string destinationDir = itksys::SystemTools::GetParentDirectory(path);
  destinationDir += "/SceneView_temp";
  // Create the directory
  if (!itksys::SystemTools::MakeDirectory(destinationDir.c_str()))
  {
    vtkErrorMacro("Failed to create directory: " << destinationDir);
    return 0;
  }

  if (!vtkArchive::UnZip(path, destinationDir.c_str()))
  {
    vtkErrorMacro("Failed to unzip the scene view svzip file: " << path);
    return 0;
  }

  vtksys::Glob glob;
  glob.RecurseOn();
  glob.RecurseThroughSymlinksOff();
  std::string globPattern(destinationDir);
  if (!glob.FindFiles(globPattern + "/*.seq.mrb"))
  {
    vtkErrorMacro("Failed to find sequence files in the scene view szvip file: " << path);
    return 0;
  }

  std::vector<std::string> files = glob.GetFiles();

  std::vector<vtkMRMLSequenceNode*> sequenceNodes;
  int i = 0;
  for (auto file : files)
  {
    vtkMRMLSequenceNode* sequenceNode = vtkMRMLSequenceNode::SafeDownCast(this->Scene->AddNewNodeByClass("vtkMRMLSequenceNode"));

    vtkSmartPointer<vtkMRMLSequenceStorageNode> sequenceStorageNode = vtkSmartPointer<vtkMRMLSequenceStorageNode>::Take(
      vtkMRMLSequenceStorageNode::SafeDownCast(sequenceNode->CreateDefaultSequenceStorageNode()));
    this->Scene->AddNode(sequenceStorageNode);
    sequenceStorageNode->SetFileName(file.c_str());
    sequenceStorageNode->ReadData(sequenceNode);
    sequenceNodes.push_back(sequenceNode);
    this->Scene->RemoveNode(sequenceStorageNode);

    std::string nodeName = vtksys::SystemTools::GetFilenameWithoutExtension(file);
    sequenceNode->SetName(nodeName.c_str());
  }

  /*vtkMRMLSequenceBrowserNode* sequenceBrowserNode = vtkMRMLSequenceBrowserNode::SafeDownCast(this->Scene->AddNewNodeByClass("vtkMRMLSequenceBrowserNode"));*/

  for (auto sequenceNode : sequenceNodes)
  {
    refNode->SetAndObserveNodeReferenceID("Sequence", sequenceNode->GetID());
  }
  return result;
}

//----------------------------------------------------------------------------
int vtkMRMLSceneViewStorageNode::WriteDataInternal(vtkMRMLNode *refNode)
{
  vtkMRMLSceneViewNode *sceneViewNode = vtkMRMLSceneViewNode::SafeDownCast(refNode);

  //if (sceneViewNode->GetScreenShot() == nullptr)
  //{
  //  // nothing to write
  //  return 1;
  //}

  std::string fullName = this->GetFullNameFromFileName();
  if (fullName.empty())
  {
    vtkErrorMacro("vtkMRMLSceneViewNode: File name not specified");
    return 0;
  }

  //std::string extension=vtkMRMLStorageNode::GetLowercaseExtensionFromFileName(fullName);

  //vtkSmartPointer<vtkImageWriter> writer;
  //if (extension == ".png")
  //{
  //  writer = vtkSmartPointer<vtkPNGWriter>::New();
  //}
  //else if (extension == ".jpg" || extension == ".jpeg")
  //{
  //  writer = vtkSmartPointer<vtkJPEGWriter>::New();
  //}
  //else if (extension == ".tiff")
  //{
  //  writer = vtkSmartPointer<vtkTIFFWriter>::New();
  //}
  //else if (extension == ".bmp")
  //{
  //  writer = vtkSmartPointer<vtkBMPWriter>::New();
  //}
  //else
  //{
  //  vtkErrorMacro( << "No file extension recognized: " << fullName.c_str() );
  //  return 0;
  //}

  int result = 1; // success by default

  //writer->SetFileName(fullName.c_str());
  //writer->SetInputData( sceneViewNode->GetScreenShot() );
  //try
  //{
  //  writer->Write();
  //}
  //catch (...)
  //{
  //  vtkDebugMacro("Cannot write scene view file '" << fullName.c_str() << "' unknown exception occurred");
  //  result = 0;
  //}
  //if (writer->GetErrorCode() != vtkErrorCode::NoError)
  //{
  //  vtkDebugMacro("Cannot write scene view file '" << fullName.c_str() << "' ("
  //    << vtkErrorCode::GetStringFromErrorCode(writer->GetErrorCode()) << ")");
  //  result = 0;
  //}

  //if (result != 0)
  //{
  //  this->StageWriteData(refNode);
  //}

  std::stringstream tempDirSS;
  tempDirSS << itksys::SystemTools::GetParentDirectory(itksys::SystemTools::GetParentDirectory(fullName.c_str()));
  tempDirSS << "/temp_" << sceneViewNode->GetName();
  std::string tempDirString = tempDirSS.str();

  // Create a bundle of the scene view node and its associated data
  std::vector<vtkMRMLNode*> nodes;
  this->GetScene()->GetNodesByClass("vtkMRMLSequenceNode", nodes);
  for (auto node : nodes)
  {
    vtkMRMLSequenceNode* sequenceNode = vtkMRMLSequenceNode::SafeDownCast(node);
    vtkSmartPointer<vtkMRMLSequenceStorageNode> sequenceStorageNode = vtkSmartPointer<vtkMRMLSequenceStorageNode>::Take(
      vtkMRMLSequenceStorageNode::SafeDownCast(sequenceNode->CreateDefaultSequenceStorageNode()));

    std::stringstream fileNameSS;
    fileNameSS << tempDirSS.str() << "/" << sequenceNode->GetName();
    fileNameSS << "." << sequenceStorageNode->GetDefaultWriteFileExtension();
    std::string fileNameString = fileNameSS.str();

    sequenceStorageNode->SetFileName(fileNameString.c_str());
    sequenceStorageNode->SetScene(this->GetScene());
    sequenceStorageNode->WriteData(sequenceNode);
  }

  std::stringstream bundleDirSS;
  bundleDirSS << itksys::SystemTools::GetParentDirectory(fullName.c_str()) << "/SceneView.svzip";
  std::string bundleDir = bundleDirSS.str();
  vtkArchive::Zip(bundleDir.c_str(), tempDirString.c_str());
  vtksys::SystemTools::RemoveADirectory(tempDirString.c_str());

  return result;
}

//----------------------------------------------------------------------------
void vtkMRMLSceneViewStorageNode::InitializeSupportedReadFileTypes()
{
  this->SupportedReadFileTypes->InsertNextValue("Scene view (.svzip)");
  this->SupportedReadFileTypes->InsertNextValue("PNG (.png)");
  this->SupportedReadFileTypes->InsertNextValue("JPG (.jpg)");
  this->SupportedReadFileTypes->InsertNextValue("JPEG (.jpeg)");
  this->SupportedReadFileTypes->InsertNextValue("TIFF (.tiff)");
  this->SupportedReadFileTypes->InsertNextValue("BMP (.bmp)");
}

//----------------------------------------------------------------------------
void vtkMRMLSceneViewStorageNode::InitializeSupportedWriteFileTypes()
{
  this->SupportedWriteFileTypes->InsertNextValue("Scene view (.svzip)");
  this->SupportedWriteFileTypes->InsertNextValue("PNG (.png)");
  this->SupportedWriteFileTypes->InsertNextValue("JPG (.jpg)");
  this->SupportedWriteFileTypes->InsertNextValue("JPEG (.jpeg)");
  this->SupportedWriteFileTypes->InsertNextValue("TIFF (.tiff)");
  this->SupportedWriteFileTypes->InsertNextValue("BMP (.bmp)");
}
