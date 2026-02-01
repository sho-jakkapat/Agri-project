from flask import Flask, jsonify, request, render_template
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime

app = Flask(__name__)
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///gps_data.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)

class GpsLog(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    latitude = db.Column(db.Float, nullable=False)
    longitude = db.Column(db.Float, nullable=False)
    gps_fix_status = db.Column(db.Integer, nullable=False)
    distance = db.Column(db.Float, nullable=True)
    action_status = db.Column(db.String(20), nullable=True)
    timestamp = db.Column(db.String, default=lambda: datetime.now().strftime('%H:%M:%S'))

    def to_dict(self):
        return {
            'id': self.id,
            'latitude': self.latitude,
            'longitude': self.longitude,
            'gps_fix_status': self.gps_fix_status,
            'distance': self.distance,
            'action_status': self.action_status,
            'timestamp': self.timestamp
        }

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/logs', methods=['GET'])
def get_logs():
    logs = GpsLog.query.order_by(GpsLog.id.desc()).all()
    return jsonify(logs=[log.to_dict() for log in logs])

@app.route('/api/log', methods=['POST'])
def add_log():
    try:
        data = request.get_json()
        new_log = GpsLog(
            latitude=data.get('latitude'),
            longitude=data.get('longitude'),
            gps_fix_status=data.get('gps_fix_status'),
            distance=data.get('distance', 0.0),
            action_status=data.get('action_status', 'N/A')
        )
        db.session.add(new_log)
        db.session.commit()
        return jsonify(new_log.to_dict()), 201
    except Exception as e:
        return jsonify({"error": str(e)}), 400

@app.route('/api/clear', methods=['DELETE'])
def clear_logs():
    try:
        db.session.query(GpsLog).delete()
        db.session.commit()
        return jsonify({"message": "Cleared"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    with app.app_context():
        db.create_all()
    app.run(host='0.0.0.0', port=5000, debug=True)