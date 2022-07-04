/*=========================================================================
/*=========================================================================

  Copyright 2005 Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

==========================================================================*/

#ifndef __vtkMRMLFocusDisplayableManager_h
#define __vtkMRMLFocusDisplayableManager_h

// MRMLDisplayableManager includes
#include "vtkMRMLAbstractThreeDViewDisplayableManager.h"
#include "vtkMRMLDisplayableManagerExport.h"

// MRML includes
#include <vtkMRMLModelNode.h>
class vtkMRMLClipModelsNode;
class vtkMRMLDisplayNode;
class vtkMRMLDisplayableNode;
class vtkMRMLTransformNode;

// VTK includes
#include "vtkRenderWindow.h"
class vtkActor;
class vtkAlgorithm;
class vtkCellPicker;
class vtkLookupTable;
class vtkMatrix4x4;
class vtkPlane;
class vtkPointPicker;
class vtkProp3D;
class vtkPropPicker;
class vtkWorldPointPicker;

class vtkOutlineGlowPass;
class vtkRenderStepsPass;

/// \brief Manage display nodes with polydata in 3D views.
///
/// Any display node in the scene that contains a valid output polydata is
/// represented into the view renderer using configured synchronized vtkActors
/// and vtkMappers.
/// Note that the display nodes must be of type vtkMRMLModelDisplayNode
/// (to have an output polydata) but the displayable nodes don't necessarily
/// have to be of type vtkMRMLModelNode.
class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLFocusDisplayableManager
  : public vtkMRMLAbstractDisplayableManager
{
public:
  static vtkMRMLFocusDisplayableManager* New();
  vtkTypeMacro(vtkMRMLFocusDisplayableManager, vtkMRMLAbstractDisplayableManager);
  void PrintSelf(ostream& os, vtkIndent indent) override;

protected:
  void UnobserveMRMLScene() override;

  void OnMRMLSceneStartClose() override;
  void OnMRMLSceneEndClose() override;
  void UpdateFromMRMLScene() override;
  void OnMRMLSceneNodeAdded(vtkMRMLNode* node) override;
  void OnMRMLSceneNodeRemoved(vtkMRMLNode* node) override;

  /// Updates Actors based on models in the scene
  void UpdateFromMRML() override;

  void ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData) override;

protected:
  vtkMRMLFocusDisplayableManager();
  ~vtkMRMLFocusDisplayableManager() override;

  vtkNew<vtkRenderer> RendererOutline;
  vtkNew<vtkRenderStepsPass> BasicPasses;
  vtkNew<vtkOutlineGlowPass> ROIGlowPass;

private:
  vtkMRMLFocusDisplayableManager(const vtkMRMLFocusDisplayableManager&) = delete;
  void operator=(const vtkMRMLFocusDisplayableManager&) = delete;

  class vtkInternal;
  vtkInternal* Internal;
};

#endif
