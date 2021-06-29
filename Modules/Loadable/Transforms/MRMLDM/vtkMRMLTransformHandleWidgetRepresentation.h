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
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/

/**
 * @class   vtkMRMLTransformHandleWidgetRepresentation
 * @brief   Default representation for the interaction widget in 3D views
 *
 * @sa
 * vtkMRMLInteractionWidgetRepresentation vtkMRMLInteractionWidget
*/

#ifndef vtkMRMLTransformHandleWidgetRepresentation_h
#define vtkMRMLTransformHandleWidgetRepresentation_h

#include "vtkSlicerTransformsModuleMRMLDisplayableManagerExport.h"

#include "vtkMRMLInteractionWidgetRepresentation.h"

#include <vtkMRMLTransformDisplayNode.h>

#include <map>

class vtkActor;
class vtkActor2D;
class vtkCellPicker;
class vtkFastSelectVisiblePoints;
class vtkGlyph3DMapper;
class vtkLabelPlacementMapper;
class vtkPolyDataMapper;
class vtkProperty;

class vtkMRMLInteractionEventData;

class VTK_SLICER_TRANSFORMS_MODULE_MRMLDISPLAYABLEMANAGER_EXPORT vtkMRMLTransformHandleWidgetRepresentation : public vtkMRMLInteractionWidgetRepresentation
{
public:
  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkMRMLTransformHandleWidgetRepresentation, vtkMRMLInteractionWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  virtual int GetActiveComponentType() override;
  virtual void SetActiveComponentType(int type) override;
  virtual int GetActiveComponentIndex() override;
  virtual void SetActiveComponentIndex(int type) override;

  virtual vtkMRMLTransformDisplayNode* GetDisplayNode();
  virtual void SetDisplayNode(vtkMRMLTransformDisplayNode* displayNode);

  virtual vtkMRMLTransformNode* GetTransformNode();

  void UpdateInteractionPipeline() override;

protected:
  vtkMRMLTransformHandleWidgetRepresentation();
  ~vtkMRMLTransformHandleWidgetRepresentation() override;

  vtkSmartPointer<vtkMRMLTransformDisplayNode> DisplayNode{ nullptr };

private:
  vtkMRMLTransformHandleWidgetRepresentation(const vtkMRMLTransformHandleWidgetRepresentation&) = delete;
  void operator=(const vtkMRMLTransformHandleWidgetRepresentation&) = delete;
};

#endif
