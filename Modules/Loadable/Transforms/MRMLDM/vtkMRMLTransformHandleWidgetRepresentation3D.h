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
 * @class   vtkMRMLTransformHandleWidgetRepresentation3D
 * @brief   Default representation for the interaction widget in 3D views
 *
 * @sa
 * vtkMRMLInteractionWidgetRepresentation vtkMRMLInteractionWidget
*/

#ifndef vtkMRMLTransformHandleWidgetRepresentation3D_h
#define vtkMRMLTransformHandleWidgetRepresentation3D_h

#include "vtkSlicerTransformsModuleMRMLDisplayableManagerExport.h"

#include "vtkMRMLTransformHandleWidgetRepresentation.h"

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

class VTK_SLICER_TRANSFORMS_MODULE_MRMLDISPLAYABLEMANAGER_EXPORT vtkMRMLTransformHandleWidgetRepresentation3D
  : public vtkMRMLTransformHandleWidgetRepresentation
{
public:
  /**
   * Instantiate this class.
   */
  static vtkMRMLTransformHandleWidgetRepresentation3D* New();

  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkMRMLTransformHandleWidgetRepresentation3D, vtkMRMLTransformHandleWidgetRepresentation);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  void SetRenderer(vtkRenderer *ren) override;

  /// Subclasses of vtkMRMLTransformHandleWidgetRepresentation3D must implement these methods. These
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

  bool AccuratePick(int x, int y, double pickPoint[3]);

  bool IsDisplayable() override;

protected:
  vtkMRMLTransformHandleWidgetRepresentation3D();
  ~vtkMRMLTransformHandleWidgetRepresentation3D() override;

  double GetViewScaleFactorAtPosition(double positionWorld[3]);

  void UpdateViewScaleFactor() override;

  void UpdateHandleSize() override;

  vtkSmartPointer<vtkCellPicker> AccuratePicker;

private:
  vtkMRMLTransformHandleWidgetRepresentation3D(const vtkMRMLTransformHandleWidgetRepresentation3D&) = delete;
  void operator=(const vtkMRMLTransformHandleWidgetRepresentation3D&) = delete;
};

#endif
