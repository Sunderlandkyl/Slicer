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
 * @class   vtkMRMLInteractionWidget
 * @brief   Process interaction events to update state of interaction widgets
 *
 * @sa
 * vtkMRMLAbstractWidget vtkSlicerWidgetRepresentation vtkSlicerWidgetEventTranslator
 *
*/

#ifndef vtkMRMLInteractionWidget_h
#define vtkMRMLInteractionWidget_h

#include "vtkMRMLDisplayableManagerExport.h"

#include "vtkMRMLAbstractWidget.h"
#include "vtkWidgetCallbackMapper.h"

class vtkMRMLAbstractViewNode;
class vtkMRMLApplicationLogic;
class vtkMRMLDisplayableNode;
class vtkMRMLInteractionEventData;
class vtkMRMLInteractionNode;
class vtkIdList;
class vtkPolyData;
class vtkMRMLInteractionWidgetRepresentation;
class vtkTransform;

class VTK_MRML_DISPLAYABLEMANAGER_EXPORT vtkMRMLInteractionWidget : public vtkMRMLAbstractWidget
{
public:
  //@{
  /**
   * Standard VTK class macros.
   */
  vtkTypeMacro(vtkMRMLInteractionWidget, vtkMRMLAbstractWidget);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  //@}

  enum
  {
    InteractionNone,
    InteractionTranslationHandle,
    InteractionRotationHandle,
    InteractionScaleHandle,
  };

  /// Widget states
  enum
  {
    WidgetStateOnTranslationHandle = WidgetStateUser, // hovering over a translation interaction handle
    WidgetStateOnRotationHandle, // hovering over a rotation interaction handle
    WidgetStateOnScaleHandle, // hovering over a scale interaction handle
    WidgetState_Last
  };

  /// Widget events
  enum
    {
    WidgetEvent_Last
    };

  virtual int GetActiveComponentType() = 0;
  virtual void SetActiveComponentType(int type) = 0;

  virtual int GetActiveComponentIndex() = 0;
  virtual void SetActiveComponentIndex(int index) = 0;

  /// Return true if the widget can process the event.
  bool CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &distance2) override;

  /// Process interaction event.
  bool ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData) override;

  /// Called when the the widget loses the focus.
  void Leave(vtkMRMLInteractionEventData* eventData) override;

  // Allows the widget to request interactive mode (faster updates)
  bool GetInteractive() override;
  // Allows the widget to request a cursor shape
  int GetMouseCursor() override;

protected:
  vtkMRMLInteractionWidget();
  ~vtkMRMLInteractionWidget() override;

  void StartWidgetInteraction(vtkMRMLInteractionEventData* eventData);
  void EndWidgetInteraction();

  virtual void TranslateWidget(double eventPos[2]);
  virtual void ScaleWidget(double eventPos[2]);
  virtual void RotateWidget(double eventPos[2]);
  virtual void ApplyTransform(vtkTransform* transform) = 0;

  // Get accurate world position.
  // World position that comes in the event data may be inaccurate, this method computes a more reliable position.
  // Returns true on success.
  // refWorldPos is an optional reference position: if point distance from camera cannot be determined then
  // depth of this reference position is used.
  bool ConvertDisplayPositionToWorld(const int displayPos[2], double worldPos[3], double worldOrientationMatrix[9],
    double* refWorldPos = nullptr);

  /// Index of the control point that is currently being previewed (follows the mouse pointer).
  /// If <0 it means that there is currently no point being previewed.
  int PreviewPointIndex;

  // Callback interface to capture events when
  // placing the widget.
  // Return true if the event is processed.
  virtual bool ProcessMouseMove(vtkMRMLInteractionEventData* eventData);
  virtual bool ProcessWidgetMenu(vtkMRMLInteractionEventData* eventData);
  virtual bool ProcessWidgetTranslateStart(vtkMRMLInteractionEventData* eventData);
  virtual bool ProcessWidgetRotateStart(vtkMRMLInteractionEventData* eventData);
  virtual bool ProcessWidgetScaleStart(vtkMRMLInteractionEventData* eventData);
  virtual bool ProcessEndMouseDrag(vtkMRMLInteractionEventData* eventData);

  // Get the closest point on the line defined by the interaction handle axis.
  // Input coordinates are in display coordinates, while output are in world coordinates.
  virtual bool GetClosestPointOnInteractionAxis(int type, int index, const double inputDisplay[2], double outputIntersectionWorld[3]);

  // Get the closest point on the plane defined using the interaction handle axis as the plane normal.
  // Input coordinates are in display coordinates, while output are in world coordinates
  virtual bool GetIntersectionOnAxisPlane(int type, int index, const double inputDisplay[2], double outputIntersectionWorld[3]);

  // Variables for translate/rotate/scale
  double LastEventPosition[2];
  double StartEventOffsetPosition[2];

private:
  vtkMRMLInteractionWidget(const vtkMRMLInteractionWidget&) = delete;
  void operator=(const vtkMRMLInteractionWidget&) = delete;
};

#endif
