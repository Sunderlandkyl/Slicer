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
 * @class   vtkMRMLInteractionWidgetRepresentation3D
 * @brief   Default representation for the interaction widget in 3D views
 *
 * @sa
 * vtkMRMLInteractionWidgetRepresentation vtkMRMLInteractionWidget
*/

#ifndef vtkMRMLInteractionWidgetRepresentation3D_h
#define vtkMRMLInteractionWidgetRepresentation3D_h

#include "vtkMRMLDisplayableManagerExport.h"
#include "vtkMRMLInteractionWidgetRepresentation.h"

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

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLInteractionWidgetRepresentation3D : public vtkMRMLInteractionWidgetRepresentation
{
public:
  /**
   * Instantiate this class.
   */
  static vtkMRMLInteractionWidgetRepresentation3D* New();

  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkMRMLInteractionWidgetRepresentation3D, vtkMRMLInteractionWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  void SetRenderer(vtkRenderer *ren) override;

  /// Subclasses of vtkMRMLInteractionWidgetRepresentation3D must implement these methods. These
  /// are the methods that the widget and its representation use to
  /// communicate with each other.
  void UpdateFromMRML(vtkMRMLNode* caller, unsigned long event, void *callData = nullptr) override;

  /// Methods to make this class behave as a vtkProp.
  void GetActors(vtkPropCollection *) override;
  void ReleaseGraphicsResources(vtkWindow *) override;
  int RenderOverlay(vtkViewport *viewport) override;
  int RenderOpaqueGeometry(vtkViewport *viewport) override;
  int RenderTranslucentPolygonalGeometry(vtkViewport *viewport) override;
  vtkTypeBool HasTranslucentPolygonalGeometry() override;

  /// Return the bounds of the representation
  double *GetBounds() override;

  void CanInteract(vtkMRMLInteractionEventData* interactionEventData,
    int &foundComponentType, int &foundComponentIndex, double &closestDistance2) override;

  /// Check if interaction with the transformation handles is possible
  virtual void CanInteractWithHandles(vtkMRMLInteractionEventData* interactionEventData,
    int& foundComponentType, int& foundComponentIndex, double& closestDistance2);

  bool AccuratePick(int x, int y, double pickPoint[3]);

protected:
  vtkMRMLInteractionWidgetRepresentation3D();
  ~vtkMRMLInteractionWidgetRepresentation3D() override;

  double GetViewScaleFactorAtPosition(double positionWorld[3]);

  void UpdateViewScaleFactor() override;

  void UpdateHandleSize() override;

  void UpdateInteractionPipeline() override;

  vtkSmartPointer<vtkCellPicker> AccuratePicker;

private:
  vtkMRMLInteractionWidgetRepresentation3D(const vtkMRMLInteractionWidgetRepresentation3D&) = delete;
  void operator=(const vtkMRMLInteractionWidgetRepresentation3D&) = delete;
};

#endif
