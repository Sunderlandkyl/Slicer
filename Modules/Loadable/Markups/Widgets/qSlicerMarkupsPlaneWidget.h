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

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

#ifndef __qSlicerMarkupsPlaneWidget_h
#define __qSlicerMarkupsPlaneWidget_h

// Qt includes
#include <QWidget>

// Markups widgets includes
#include "qSlicerMarkupsAdditionalOptionsWidget.h"
#include "qSlicerMarkupsModuleWidgetsExport.h"

// CTK includes
#include <ctkPimpl.h>
#include <ctkVTKObject.h>

class vtkMRMLAnnotationPlaneNode;
class vtkMRMLNode;
class vtkMRMLMarkupsPlaneNode;
class qSlicerMarkupsPlaneWidgetPrivate;

class Q_SLICER_MODULE_MARKUPS_WIDGETS_EXPORT qSlicerMarkupsPlaneWidget : public qSlicerMarkupsAdditionalOptionsWidget
{
  Q_OBJECT
  QVTK_OBJECT

public:
  typedef qSlicerMarkupsAdditionalOptionsWidget Superclass;
  qSlicerMarkupsPlaneWidget(QWidget* parent=nullptr);
  ~qSlicerMarkupsPlaneWidget() override;

  /// Returns the current MRML Plane node
  vtkMRMLMarkupsPlaneNode* mrmlPlaneNode()const;

  /// Gets the name of the additional options widget type
  const QString getAdditionalOptionsWidgetTypeName() override { return "Plane"; }

  /// Updates the widget based on information from MRML.
  void updateWidgetFromMRML() override;

  /// Checks whether a given node can be handled by the widget
  bool canManageMRMLMarkupsNode(vtkMRMLMarkupsNode *markupsNode) const override;

public slots:

  /// Set the MRML node of interest
  void setMRMLMarkupsNode(vtkMRMLMarkupsNode* node) override;

  /// Sets the vtkMRMLMarkupsNode to operate on.
  void setMRMLMarkupsNode(vtkMRMLNode* node) override;

protected slots:
  /// Internal function to update type of Plane
  void onPlaneTypeIndexChanged();
  void onPlaneSizeModeIndexChanged();
  void onPlaneSizeSpinBoxChanged();

protected:
  qSlicerMarkupsPlaneWidget(qSlicerMarkupsPlaneWidgetPrivate &d, QWidget* parent=nullptr);
  void setup();

private:
  Q_DECLARE_PRIVATE(qSlicerMarkupsPlaneWidget);
  Q_DISABLE_COPY(qSlicerMarkupsPlaneWidget);

};

#endif
