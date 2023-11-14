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
 * @class   vtkSlicerMarkupsInteractionWidgetRepresentation
 * @brief   Abstract representation for the transform handle widgets
 *
 * @sa
 * vtkMRMLInteractionWidgetRepresentation
*/

#ifndef vtkSlicerMarkupsInteractionWidgetRepresentation_h
#define vtkSlicerMarkupsInteractionWidgetRepresentation_h

#include "vtkSlicerMarkupsModuleVTKWidgetsExport.h"

// MRMLDM includes
#include "vtkMRMLInteractionWidgetRepresentation.h"

// MRML includes
#include <vtkMRMLMarkupsDisplayNode.h>

class VTK_SLICER_MARKUPS_MODULE_VTKWIDGETS_EXPORT vtkSlicerMarkupsInteractionWidgetRepresentation : public vtkMRMLInteractionWidgetRepresentation
{
public:
  /**
   * Instantiate this class.
   */
  static vtkSlicerMarkupsInteractionWidgetRepresentation* New();

  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkSlicerMarkupsInteractionWidgetRepresentation, vtkMRMLInteractionWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  virtual int GetActiveComponentType() override;
  virtual void SetActiveComponentType(int type) override;
  virtual int GetActiveComponentIndex() override;
  virtual void SetActiveComponentIndex(int type) override;

  virtual vtkMRMLMarkupsDisplayNode* GetDisplayNode();
  virtual void SetDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode);

  virtual vtkMRMLMarkupsNode* GetMarkupsNode();

  void UpdateInteractionPipeline() override;

  double GetInteractionScale() override; // Size relative to screen
  double GetInteractionSize() override; // Size in mm
  bool GetInteractionSizeAbsolute() override; // True -> size in mm; false -> relative to screen

  bool IsDisplayable() override;

protected:
  vtkSlicerMarkupsInteractionWidgetRepresentation();
  ~vtkSlicerMarkupsInteractionWidgetRepresentation() override;

  vtkSmartPointer<vtkMRMLMarkupsDisplayNode> DisplayNode{ nullptr };

private:
  vtkSlicerMarkupsInteractionWidgetRepresentation(const vtkSlicerMarkupsInteractionWidgetRepresentation&) = delete;
  void operator=(const vtkSlicerMarkupsInteractionWidgetRepresentation&) = delete;
};

#endif
